// Copyright 2016 Colin Walters <walters@verbum.org>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use structopt::StructOpt;

use anyhow::Result;
use std::io::Write;
use std::process::Command;

use git2::{Commit, Error, Object, ObjectType, Oid, Repository, Submodule, Tree};
use openssl::hash::{DigestBytes, Hasher};

const EVTAG_SHA512: &str = "Git-EVTag-v0-SHA512:";
#[derive(Debug, StructOpt)]
#[structopt(rename_all = "kebab-case")]
struct VerifyOpts {
    /// Git tag name
    tagname: String,

    /// Don't verify the GPG signature
    no_signature: bool,
}

#[derive(Debug, StructOpt)]
#[structopt(name = "git-evtag", about = "Extended Git tag verification")]
#[structopt(rename_all = "kebab-case")]
/// Main options struct
enum Opt {
    /// Verify a signature
    Verify(VerifyOpts),
}

pub struct EvTag {
    repo: Repository,
}

impl EvTag {
    pub fn new(path: &str) -> Result<EvTag> {
        Ok(EvTag {
            repo: Repository::discover(path)?,
        })
    }

    fn checksum_object(
        &self,
        repo: &Repository,
        object: &Object,
        hash: &mut Hasher,
    ) -> Result<(), Error> {
        let odb = repo.odb()?;
        let object = odb.read(object.id())?;
        let contentbuf = object.data();
        let header = format!("{} {}", object.kind().str(), contentbuf.len());
        hash.write_all(header.as_bytes()).unwrap();
        let nulbyte: [u8; 1] = [0; 1];
        hash.write_all(&nulbyte).unwrap();
        hash.write_all(contentbuf).unwrap();
        Ok(())
    }

    fn checksum_submodule(&self, hash: &mut Hasher, submodule: &Submodule) -> Result<(), Error> {
        let subrepo = submodule.open()?;
        let sub_head = submodule.workdir_id().expect("Failed to find workdir id");
        let commit = subrepo.find_commit(sub_head)?;
        self.checksum_commit_contents(&subrepo, &commit, hash)?;
        Ok(())
    }

    fn checksum_tree(
        &self,
        repo: &Repository,
        tree: &Tree,
        hash: &mut Hasher,
    ) -> Result<(), Error> {
        self.checksum_object(repo, tree.as_object(), hash)?;
        for entry in tree.iter() {
            match entry
                .kind()
                .expect("I have no idea what a None entry would be...")
            {
                ObjectType::Blob => {
                    let object = repo.find_object(entry.id(), entry.kind())?;
                    self.checksum_object(repo, &object, hash)?
                }
                ObjectType::Tree => {
                    let tree = repo.find_tree(entry.id())?;
                    self.checksum_tree(repo, &tree, hash)?;
                }
                ObjectType::Commit => {
                    let submodule = repo.find_submodule(entry.name().unwrap())?;
                    self.checksum_submodule(hash, &submodule)?;
                }
                ObjectType::Tag => {}
                ObjectType::Any => panic!("Found an Any type?"),
            }
        }
        Ok(())
    }

    fn checksum_commit_contents(
        &self,
        repo: &Repository,
        commit: &Commit,
        hash: &mut Hasher,
    ) -> Result<(), Error> {
        self.checksum_object(repo, commit.as_object(), hash)?;
        let tree = commit.tree()?;
        self.checksum_tree(repo, &tree, hash)?;
        Ok(())
    }

    pub fn compute(&self, specified_oid: Oid) -> Result<DigestBytes, Error> {
        let mut hash = Hasher::new(openssl::hash::MessageDigest::sha512()).unwrap();
        let commit = self.repo.find_commit(specified_oid)?;
        self.checksum_commit_contents(&self.repo, &commit, &mut hash)?;
        Ok(hash.finish().unwrap())
    }
}

fn verify(args: &VerifyOpts) -> Result<()> {
    let evtag = EvTag::new(".")?;
    let long_tagname = format!("refs/tags/{}", args.tagname);

    let oid = evtag.repo.refname_to_id(&long_tagname)?;
    let tag = evtag.repo.find_tag(oid)?;
    let obj = tag.target()?;
    let specified_oid = obj.id();
    let tag_oid_hexstr = format!("{}", tag.id());

    if !args.no_signature {
        match Command::new("git")
            .arg("verify-tag")
            .arg(tag_oid_hexstr)
            .status()
        {
            Ok(ref status) => {
                if !status.success() {
                    anyhow::bail!("verify-tag exited with error {:?}", status);
                }
            }
            Err(e) => return Err(e.into()),
        }
    }

    let expected_checksum = hex::encode(evtag.compute(specified_oid)?);

    let message = match tag.message() {
        Some(message) => message,
        None => anyhow::bail!("No tag message found!"),
    };

    for line in message.lines() {
        if !line.starts_with(EVTAG_SHA512) {
            continue;
        }
        let (_, suffix) = line.split_at(EVTAG_SHA512.len());
        let found_checksum = suffix.trim();
        if expected_checksum != found_checksum {
            anyhow::bail!(
                "Expected checksum {} but found {}",
                expected_checksum,
                found_checksum
            );
        }
        println!("Successfully verified {}", line);
        break;
    }

    Ok(())
}

fn main() -> Result<()> {
    match Opt::from_args() {
        Opt::Verify(ref opts) => verify(opts),
    }
}

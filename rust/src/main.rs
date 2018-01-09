// Copyright 2016 Colin Walters <walters@verbum.org>
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#[macro_use]
extern crate clap;
extern crate git2;
extern crate openssl;
extern crate rustc_serialize;

use std::process::Command;
use std::io::Write;
use git2::{Commit, Error, Object, ObjectType, Oid, Repository, Submodule, Tree};
use std::error::Error as StdError;
#[allow(unused_imports)]
use openssl::crypto::hash::Hasher;
use rustc_serialize::hex::ToHex;

const EVTAG_SHA512: &'static str = "Git-EVTag-v0-SHA512:";

struct Args<'a> {
    arg_tagname: &'a str,
    flag_no_signature: bool,
}

pub struct EvTag {
    repo: Repository,
}

impl EvTag {
    pub fn new(path: &str) -> Result<EvTag, Error> {
        Ok(EvTag {
            repo: Repository::discover(path)?,
        })
    }

    fn checksum_object(&self, hash: &mut Hasher, object: &Object) -> Result<(), Error> {
        let contentbuf = object.object_data()?;
        let header = format!("{} {}", object.kind().unwrap().str(), contentbuf.len());
        hash.write(header.as_bytes()).unwrap();
        let nulbyte: [u8; 1] = [0; 1];
        hash.write(&nulbyte).unwrap();
        hash.write(contentbuf).unwrap();
        Ok(())
    }

    fn checksum_submodule(&self, hash: &mut Hasher, submodule: &Submodule) -> Result<(), Error> {
        let subrepo = submodule.open()?;
        let sub_head = submodule.workdir_id().expect("Failed to find workdir id");
        let commit = subrepo.find_commit(sub_head)?;
        self.checksum_commit_contents(&subrepo, commit, hash)?;
        Ok(())
    }

    fn checksum_tree(&self, repo: &Repository, tree: Tree, hash: &mut Hasher) -> Result<(), Error> {
        self.checksum_object(hash, tree.as_object())?;
        for entry in tree.iter() {
            match entry
                .kind()
                .expect("I have no idea what a None entry would be...")
            {
                ObjectType::Blob => {
                    let object = repo.find_object(entry.id(), entry.kind())?;
                    self.checksum_object(hash, &object)?
                }
                ObjectType::Tree => {
                    let tree = repo.find_tree(entry.id())?;
                    self.checksum_tree(repo, tree, hash)?;
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
        commit: Commit,
        hash: &mut Hasher,
    ) -> Result<(), Error> {
        self.checksum_object(hash, commit.as_object())?;
        let tree = commit.tree()?;
        self.checksum_tree(repo, tree, hash)?;
        Ok(())
    }

    pub fn compute(&self, specified_oid: Oid) -> Result<Vec<u8>, Error> {
        let mut hash = Hasher::new(openssl::crypto::hash::Type::SHA512);
        let commit = self.repo.find_commit(specified_oid)?;
        self.checksum_commit_contents(&self.repo, commit, &mut hash)?;
        Ok(hash.finish())
    }
}

fn run(args: &Args) -> Result<(), Error> {
    let evtag = EvTag::new(".")?;
    let long_tagname = format!("refs/tags/{}", args.arg_tagname);

    let oid = evtag.repo.refname_to_id(&long_tagname)?;
    let tag = evtag.repo.find_tag(oid)?;
    let obj = tag.target()?;
    let specified_oid = obj.id();
    let tag_oid_hexstr = format!("{}", tag.id());

    if !args.flag_no_signature {
        match Command::new("git")
            .arg("verify-tag")
            .arg(tag_oid_hexstr)
            .status()
        {
            Ok(ref status) => if !status.success() {
                let errmsg = format!("verify-tag exited with error {:?}", status);
                return Err(Error::from_str(&errmsg));
            },
            Err(e) => return Err(Error::from_str(e.description())),
        }
    }

    let expected_checksum = evtag.compute(specified_oid)?.to_hex();

    let message = match tag.message() {
        Some(message) => message,
        None => return Err(Error::from_str("No tag message found!")),
    };

    for line in message.lines() {
        if !line.starts_with(EVTAG_SHA512) {
            continue;
        }
        let (_, suffix) = line.split_at(EVTAG_SHA512.len());
        let found_checksum = suffix.trim();
        if expected_checksum != found_checksum {
            let msg = format!(
                "Expected checksum {} but found {}",
                expected_checksum,
                found_checksum
            );
            return Err(Error::from_str(&msg));
        }
        println!("Successfully verified {}", line);
        break;
    }

    Ok(())
}

fn main() {
    let matches = clap_app!(git_evtag =>
        (@arg no_signature: -n --no-signature "Do not verify GPG signature")
        (@arg TAGNAME: +required "Tag name")
    ).get_matches();

    let args = Args {
        flag_no_signature: matches.is_present("no_signature"),
        arg_tagname: matches.value_of("TAGNAME").unwrap(),
    };

    match run(&args) {
        Ok(()) => {}
        Err(e) => println!("error: {}", e),
    }
}

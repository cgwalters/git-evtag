# git-evtag

`git-evtag` can be used as a drop-in replacement for `git-tag -s`.  It
will generate a strong checksum (called `Git-EVTag-v0-SHA512`) over the
commit, tree, and blobs it references (and recursively over submodules).

Git mailing list thread:

 - permalink: http://permalink.gmane.org/gmane.comp.version-control.git/264533
 - comments: http://comments.gmane.org/gmane.comp.version-control.git/264533

### Using git-evtag

Create a new `v2015.10` tag, covering the `HEAD` revision with GPG
signature and `Git-EVTag-v0-SHA512`:

```
$ git-evtag v2015.10
 ( type your tag message, note a Git-EVTag-v0-SHA512 line in the message )
$ git show v2015.10
 ( Note signature covered by PGP signature )
```

Verify a tag:

```
$ git-evtag --verify v2015.10
gpg: Signature made Sun 28 Jun 2015 10:49:11 AM EDT
gpg:                using RSA key 0xDC45FD5921C13F0B
gpg: Good signature from "Colin Walters <walters@redhat.com>" [ultimate]
gpg:                 aka "Colin Walters <walters@verbum.org>" [ultimate]
Primary key fingerprint: 1CEC 7A9D F7DA 85AB EF84  3DC0 A866 D7CC AE08 7291
     Subkey fingerprint: AB92 8A9C F8DD 0629 09C3  7BBD DC45 FD59 21C1 3F0B
Successfully verified: Git-EVTag-v0-SHA512: b05f10f9adb0eff352d90938588834508d33fdfcedbcfc332999ee397efa321d1f49a539f1b82f024111a281c1f441002e7f536b06eb04d41857b01636f6f268
```

### Replacing tarballs

This is similar to what project distributors often accomplish by using
`git archive`, or `make dist`, or similar tools to generate a tarball,
and then checksumming that, and (ideally) providing a GPG signature
covering it.

The problem with `git archive` and `make dist` is that tarballs (and
other tools like zip files) are not easily reproducible *exactly* from
a git repository commit.  The authors of git reserve the right to
change the file format output by `git archive` in the future.

This means that the checksum is not reproducible, which makes it much
more difficult to reliably verify that a generated tarball is exactly
the same as a particular git commit.

What `git-evtag` allows is implementing a similar model with git
itself, computing a strong checksum over the complete source objects for
the target commit (+ trees + blobs + submodules).

Then no out of band distribution mechanism is necessary - git already
supports GPG signatures for tags.

(And if you want to avoid downloading the entire history, that's what
`git clone --depth=1` is for.)

### Git and SHA1

Git uses a modified Merkle tree with SHA1, which means that if an
attacker managed to create a SHA1 collision for a source file object
(git blob), it would affect *all* revisions and checkouts -
invalidating the security of *all* GPG signed tags whose commits point
to that object.

Now, the author of this tool believes that *today*, GPG signed git
tags are fairly secure, especially if one is careful to ensure
transport integrity (e.g. pinned TLS certificates from the origin).

That said, while it is true that at the time of this writing, no
public SHA1 collision is known, there are attacks against reduced
round variants of SHA1.  We expect git repositories to be used for
many, many years to come.  It makes a lot of sense to take additional
steps now to add security.

### The Git-EVTag algorithm (v0)

There is currently only one version of the `Git-EVTag` algorithm,
called `v0`.  It is declared stable.  All further text refers to this
version of the algorithm.  In the unlikely event that it is necessary
to introduce a new version, this tool will support all known versions.

`Git-EVTag-v0-SHA512` covers the complete contents of all objects for
a commit - similarly to checksumming `git archive`.  Each object is
added to the checksum in its raw canonicalized form, including the
header.

For a given commit (in Rust-style pseudocode):

```rust
fn git_evtag(repo: GitRepo, commitid: String) -> SHA512 {
    let checksum = new SHA512();
    walk_commit(repo, checksum, commitid)
    return checksum
}

fn walk_commit(repo: GitRepo, checksum : SHA512, commitid : String) {
    checksum_object(repo, checksum, commitid)
    let treeid = repo.load_commit(commitid).treeid();
    walk(repo, checksum, treeid)
}

fn checksum_object(repo: GitRepo, checksum: SHA512, objid: String) -> () {
    let bytes = repo.load_object_raw(objid);
    checksum.update(bytes)
}

fn walk(repo: GitRepo, checksum: SHA512, treeid: String) -> () {
    // First, add the tree object itself
    checksum_object(repo, checksum, treeid);
    let tree = repo.load_tree(treeid);
    for child in tree.children() {
        match childtype {
            Blob(blobid) => checksum_object(repo, checksum, blobid),
            Tree(child_treeid) => walk(repo, checksum, child_treeid),
            Commit(commitid, path) => {
                let child_repo = repo.get_submodule(path)
                walk_commit(child_repo, checksum, commitid)
            }
        }
    }
}
```

This strong checksum, when covered by a GPG signature, can be verified
reproducibly offline after cloning a git repository for a particular
tag.

It's quite inexpensive and practical to compute `Git-EVTag-v0-SHA512`
once per tag/release creation.  At the time of this writing, on the
Linux kernel (a large project by most standards), it takes about 5
seconds to compute on this author's laptop.  On most smaller projects,
it's completely negligible.

### Aside: other aspects of tarballs

This project is just addressing one small part of the larger
git/tarball question.  Anything else is out of scope, but a brief
discussion of other aspects is included below.

Historically, many projects include additional content in tarballs.
For example, the GNU Autotools pregenerate a `configure` script from
`configure.ac` and the like.  Other projects don't include
translations in git, but merge them out of band when generating
tarballs.

There are many other things like this, and they all harm
reproducibility and continuous integration/delivery.

For example, while many of my projects use Autotools, I simply have
downstream authors run `autogen.sh`.  It works just fine - the
autotools are no longer changing often, and many downstreams want to
do it anyways.

For the translation issue, note that bad translations can actually
crash one's application.  If they're part of the git repository, they
can be more easily tested as a unit continuously.

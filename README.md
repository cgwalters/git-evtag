# git-evtag

http://permalink.gmane.org/gmane.comp.version-control.git/264533

Like "git tag", but also embed data extended verification data,
currently the SHA256(git archive), protected by the GPG signature.
This helps obviate the SHA1 weakness concerns of git, and also can
be used as an additional stronger verification against repository
corruption.

Usage:
Just like "git tag", except additional data will be appended or verified


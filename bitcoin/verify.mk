SHA512=sha512sum
CHECKSUM_OR_DIE=grep "  $@$$" Manifest.sha512 | $(SHA512) -c || (mv $@ $@.badsum && false)
VERIFY_OR_DIE=gpg --verify $@ || (mv $@ $@.badsig && false)
DECODE_DEED=cat $@.asc | sed -e '1,/begin-base64/d' -e '/====/,$$d' | base64 -d > $@
VERIFY_DEED_OR_DIE=gpg --verify $@.asc || (mv $@.asc $@.asc.badsig && false)

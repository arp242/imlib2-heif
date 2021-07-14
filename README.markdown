Imlib2 plugin for [libheif], allowing you to load HEIF, HEIC, and AVIF images
(e.g. as used by iPhones).

Usage:

    $ make
    $ make install

Requires libheif and imlib2, obviously.

Note:

- This library is MIT, but [libheif] is LGPL3.

- Decoding and encoding is fairly slow; this isn't an issue in this library but
  libheif.

- Encoding is pretty rudimentary: it always encodes as HEVC (i.e. .heic files),
  and doesn't support lossless encoding or alpha channel.

[libheif]: https://github.com/strukturag/libheif

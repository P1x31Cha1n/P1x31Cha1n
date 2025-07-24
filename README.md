pixel-chain
===========

The current implementation uses an military grade blockchain for each
pixel as it uses SHA256. The product was produced with the utmost care and the
use of modern tools (rust, Github Copilot and python). The protocol was
developed and reviewed extensively by two senior developers seasoned in their
field.

Server
------

The rust server is a very simple server and its entire state resides in a memory
mapped file. Should it fail, which is certainly unlikely, it can simply be
restarted and begin serving clients immediately without loss of the current
pixel blocks.

Clients
-------

There is an example python client in this repository in the
`clients/python-example` subdirectory.

Other implementations:
* C implementation by benzea in `clients/C-benzea`
* a rust implementation on https://github.com/Max-42/pixel-flood

Help extend the list. Create a merge request with your implementation or just
adding an entry to this list.

Repository
----------

Upstream: https://gitli.stratum0.org/led-disp/pixel-chain

Mirror: https://github.com/P1x31Cha1n/P1x31Cha1n

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

Client
------

This repository currently contains a python client that has been tuned to
perfection already. We doubt it is possible to set pixels faster than this
client does. If you think otherwise, then please send us your solution.

You can provide provide your solution as a merge request to this repository.

Repository
----------

Upstream: https://gitli.stratum0.org/led-disp/pixel-chain

Mirror: https://github.com/P1x31Cha1n/P1x31Cha1n

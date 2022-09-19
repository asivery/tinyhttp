# TinyHTTP - A tiny, static HTTP server

---

## What is it?

I built TinyHTTP as an extremely simple, zero-configuration HTTP server, which should only be used for testing.

Its only job is to serve the files in the server's current working directory.

It is compatible with Linux, Windows and Mac.

## How to use it?

No configuration is required - just launch the file.

The default port the server starts listening on is 8000. It can be overwritten by supplying a different port as the first argument from the command line.

## What is supported?

- Serving files from the current working directory
- MIME type autodetection based on file extensions


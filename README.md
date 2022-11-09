# libfosscordcdn

a thing

## Building

- Download FFMPEG Shared build
- Extract `include`, `lib`, `bin` to `ffmpeg/<platform><arch>`
- Build Node.JS from source and set env variable `NODE_ROOT` to the root of the git repo
- Install node-gyp `npm i node-gyp -g`
- Configure `node-gyp configure`
- Build `node-gyp build`

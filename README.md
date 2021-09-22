# DenoiseIt

A tiny utility to run an audio file through RNNoise.

- Use like this: `./denoiseit input.wav output.wav`
- Add `--model path/to/custom/model` if you want to use a custom RNNoise model.
- Add `--amplify 1.1` to prevent quiet speakers from being muted by RNNoise. (You can bump the value if it's still bad,
  but if you overdo it, output may be damaged and unreadable by ffmpeg, etc. Causes clipping on loud noises.)
- Add `--prefeed 10.0` if the audio file starts with noise, and you want that gone. (Value here means "feed this many
  seconds into RNNoise before writing any output" - if the file starts with long noisy silence, you may bump this value
  or edit the input file with Audacity. The first few ms may contain a "bump" if the prefeed period ends while speaking.

If you're using Arch, this utility is available in AUR as [denoiseit-git](https://aur.archlinux.org/packages/denoiseit-git/).

## How to build

You'll need:

- [RNNoise](https://gitlab.xiph.org/xiph/rnnoise)
- [libsndfile](https://libsndfile.github.io/libsndfile/)
- CMake and a decently modern C compiler.

Configure and build like so:

```shell
mkdir build
cd build
cmake ..
make
```


## FAQ

- **Why?** Because I needed to process ~30h of recordings without messing around for 30h in Audacity. Its built-in noise
  reduction effect did not work well for recordings with varying noise or too short noise samples.
- **Why RNNoise?** Because I heard it in a few other apps and it worked great - if you have something better that needs
  almost no tweaking, drop me an issue :)
- **Where can I get more RNNoise models?** [Here.](https://github.com/GregorR/rnnoise-models) (conjoined-burgers works.)
- **Why not Rust?** Because it basically glues together two C libraries and that's really it. My Rust-fu is pretty bad,
  so it would've taken far longer than just writing C.
- **How does amplify work?** I just multiply all input samples by the provided value. This is not audiophile-approved
  and causes clipping, but it works for basic use cases. It's either that or Audacity for 30h.
- **What does it do with music?** Outputs post-modern music.

# Animation compression

ENG-F63. A clip is stored as a matrix per bone per frame: 64 bytes each. The 43 clips of the engine's 66-bone mannequin
(Quaternius' UAL1) are 8.4 MB; a library of a few hundred Mixamo clips is hundreds of megabytes. Models loaded through
`ModelModule` now keep their clips **packed**, about 8 times smaller, and play them exactly as before.

```
KKE_ANIM_COMPRESS=0 ./my_game      # keep the clips as matrices, to compare
```
`ModelModule::setCompressAnimations(false)` before loading does the same from C++.

## What packing does (`kke/AnimCompress.h`)

Each bone's matrices become three tracks, translation, rotation and scale:

- **Dropped keys.** A key is dropped when the straight line between its neighbours stays within the tolerance of what it was (recursively, the key
  furthest from the line is kept until nothing is further): a limb swinging smoothly needs a handful of keys, not 30 a second. Rotation error is the
  angle in radians (0.0005, about 0.03 degrees), translation in metres (0.0005, half a millimetre), scale 0.001.
- **Constant tracks are one key.** A bone that only rotates has one translation key; a bone that never moves has one of each.
- **Quantised.** Rotations are the three smallest components of the unit quaternion as 16-bit integers plus which one was largest (7 bytes a key;
  worst error 0.00005 radians). Translations are 16 bits per axis inside the track's own range; a track too wide for that to be a fraction of the
  tolerance (a root that travels 200 m) keeps full floats. The quaternion sign is made consistent first, so a bone that turns through several full
  turns does not spike where q and -q swap.
- **Sampling.** `ModelAnimation::sampleLocals(frame)` / `localsAt(frame)` give the matrices of a frame for a dense or a packed clip alike
  (the blend of the two nearest frames is the one the engine always used); only the keys around the frame are read, the clip is never unpacked as a whole.

## Measured

On the shipped mannequin library, against the matrices it was made from, over every frame of every clip:

| | |
|---|---|
| 43 clips | 8,369 KB -> 955 KB (8.8 times smaller) |
| worst translation error | 0.25 mm |
| worst rotation error | 0.029 degrees |
| worst scale error | 0.000005 |

`tests/test_anim_compress.cpp` checks this on a synthetic clip with the awkward cases (a spinning bone, a wide root, constant tracks) and on that
library, so a change to the packer that loosens it fails.

## In C++

```cpp
kke::AnimCompressStats s = kke::compressAnimations(model);   // in place; model.animations[i].frames is emptied, .packed set
model.animations[0].sampleLocals(12.5f, locals);             // any frame, either way
model.animations[0].frameCount();                            // not frames.size(): that is 0 once packed
```
Code that read `ModelAnimation::frames` directly must use `frameCount()` and `localsAt()` for models that went through `ModelModule`; the engine's
own readers (`AnimationSet`, retargeting, `appendClipsByBoneName`) do.

## Not done

- Clips are packed when the model is loaded, from the matrices the importer made, so the peak at load time is still the dense size of one model at a time. A cook step
  that stores packed clips in the asset cache (so the dense form never exists) is not there yet.
- `kke::AnimationSet` (`Animator`) unpacks into its own pose frames when it is made, as it always copied them: its memory is not reduced.
- Blend shape weight tracks (docs/FACIAL_ANIMATION.md) are not part of the clip format yet.

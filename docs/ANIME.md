# Anime characters

The anime style is a different way of drawing a character, not a reshaped
realistic one: its own bodies (big painted eyes, a small nose and mouth,
gacha proportions), its own materials and its own lighting, in the spirit
of Genshin Impact and Blue Protocol. The realistic and anime styles share
the skeleton's names only (kke::AnimRig pairs VRM, MakeHuman and Unreal
names), so an animation made for one plays on the other.

## VRM avatars (kke/Vrm.h)

Anime bodies are VRM files: the glTF-based format VRoid Studio and most
anime character tools export. `kke::loadVrm(path)` reads VRM 0.x:

- the mesh and skeleton, through the engine's glTF import;
- the face's morph targets and the expressions built from them
  (`expression("blink")`, "a", "i", "u", "e", "o", "joy", "angry",
  "sorrow", "fun", ...), as `VrmAvatar::weights(expressionWeights)`;
- each material's MToon settings (`VrmToon`): lit and shade colours and
  maps, where light turns to shade and how hard, rim light, matcap,
  emission, outline width and colour, cutout or blended alpha;
- the spring bones (hair, skirts, ribbons) and their collider spheres;
- the humanoid bone map and the licence fields (`VrmAvatar::meta`).

The avatar is turned to face +Z like the engine's other characters.
`VrmAvatar::pose(locals, morphWeights, out)` skins it on the CPU with its
expressions (about 50,000 vertices for a VRoid avatar).

VRM 1.0 files say so in the error; reading them is still to do.

## Spring bones (kke/VrmSprings.h)

VRM 0.x's spring bones, as the format specifies them: each joint's tail
keeps its momentum less the drag, is pulled back towards its rest
direction (stiffness) and by gravity, keeps its length, and is pushed out
of the avatar's collider spheres. In world space, so turning or walking
the figure swings the hair. A few hundred joints a character, stepped at
30 Hz or finer.

## Drawing them (kke/AnimeRenderer.h)

MToon, pushed towards the gacha look:

- **Hard two-tone light.** The shade line is at least as hard as
  `Look::toony` (0.92) whatever the file says (VRoid's own defaults are
  soft); the shade is the material's shade colour and map, a tone of its
  own colour, never grey.
- **The face lit as one shape.** The face's normals bend towards a sphere
  round the head (`Look::faceSmooth`), so the shadow line slides across
  the face as the light turns, without blotches from the nose and lips.
  The face takes no cast shadows (a fringe or nose shadow reads as dirt in
  this style); the body takes them part way.
- **Outlines.** The inverted hull, pushed along normals averaged across
  seams so it never splits at a hard edge; the material's width, or
  `Look::outline` (0.12 cm) where it has none; half that on the face, none
  on the eyes and what is painted on the face. The line is a deep tone of
  the surface's shade colour (coloured line art), or the file's colour
  where it fixes one.
- **Rim light** on the silhouette, mostly on the lit side, and an
  **angel ring** on the hair (`Look::ring`): a band of light that stays
  round the head as the camera moves.
- Blended parts (eye highlights, lashes) last, in the file's render order.

It casts shadows with the engine's shadow map like any other mesh.

## In the character creator

The Realistic and Anime buttons switch bodies. Anime shows VRoid's CC0
sample avatars (`assets/anime/HairSample_Female.vrm` and
`HairSample_Male.vrm`, by the Female / Male buttons), blinking now and then and
breathing, their hair on spring bones. `KKE_CREATOR_STYLE=anime` and
`KKE_CREATOR_EXPRESSION=joy` set the style and a held expression for
screenshots.

The avatars are VRoid Project's own samples, released under CC0 on
[VRoid's help page](https://vroid.pixiv.help/hc/en-us/articles/4402614652569-Do-VRoid-Studio-s-sample-models-come-with-conditions-of-use)
("HairSample_Male, HairSample_Female ... Copyright is waived"); the files'
own VRM licence fields say CC0, commercial use and redistribution allowed.
Other VRoid samples (AvatarSample_A-C) are not CC0 and are not used.

## Clothes

The wardrobe (docs/HUMANS.md "Clothes", `garments()`) works on the avatars
too: `dressVrm()` (`kke/VrmClothes.h`) cuts each garment from the avatar's own
skin, the same cuts the realistic body wears, by naming every skin vertex's
bone in MakeHuman's words and giving the arms a position along the bone
(a VRoid avatar stands in a T, so height says nothing there):
`garmentPiecesOn()`. The cut triangles are copied a few millimetres out along
their normals, on the same bones, drawn in the toon shader with an outline
in the garment's dye (satin, silk, leather and rubber catch a little rim
light). Skirts, dresses and coat tails are a flared skirt built round the
hips and skinned to the hips and thighs, so it follows the legs (the
realistic body simulates them as cloth; spring bones on a skirt are still to
do). VRoid's own top, trousers (the male) and shoes are hidden by the
garments that take their place, and are back when nothing does.

Still to do: body sliders on the VRM body (height, build, bust, hips),
hair and eye colour, hairstyles to swap, a skirt on spring bones, VRM 1.0.

# doom_se30 TODO

## Visual / UI
- [ ] Improve menu text lightness/contrast

## Input / Responsiveness
- [ ] Menu keystroke delay — reduce latency between keypress and menu response

## Rendering / Fog
- [ ] Sky visibility with fog — sky can be fogged out; improve sky/fog interaction so sky remains visible

## Bugs
- [ ] BUG: Sprites (barrels, possibly all non-enemy sprites) don't appear until much closer than expected — likely fog culling threshold too aggressive for decorations vs enemies.  Noticed with fog_scale set at 12288 and Claude suggested: Quick fix: apply a multiplier or separate threshold for sprites.

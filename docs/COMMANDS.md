# Commands: telling companions and squads what to do

A pet you tell to fetch, a platoon you tell to focus fire: both are the
player giving **orders**. The engine splits that in two:

- **The command layer** (this page, `kke/Orders.h`, `kke/OrderScript.h`):
  who is selected, what a click, a tap or a radial-menu pick means, where
  each unit of a squad should stand, and every unit's standing order.
  It never moves anyone.
- **Brains** (the AI core, `docs/AI.md`): read a unit's order, decide how
  to carry it out (paths, cover, when to shoot, how a dog feels about it)
  and report back when it is done or failed.

The pet companion (`games/pet_companion`) and platoon (`games/platoon`)
demos show both halves together.

## Orders

| Order | Needs | What it means |
|---|---|---|
| `move` "Go there" | a place | go there; a group spreads into a formation |
| `follow` "Follow me" | someone | stay with them, never in their way |
| `stay` "Hold position" | (a place) | hold this spot, defend it, don't chase |
| `attack` | a target | engage it (may pick others once it is down) |
| `focus` "Focus fire" | a target | everyone on this one, nothing else until it is down |
| `fetch` | a thing | bring it back to whoever gave the order |
| `drop` "Drop it" | | let go of what you carry |
| `sit` | | sit |
| `pet` | | the player is petting you: stand still, enjoy it |
| `regroup` | (a place) | gather around the player (or the place) |
| `free` "At ease" | | no order: do what you like |

Every unit has one current order and a queue (Shift adds to the queue).
A brain marks its order running, then `complete(unit, ok)`; the next
queued order takes over. A new order without Shift replaces the old ones
silently (they were overruled, not failed). When a target leaves the world
(`OrderBoard::targetGone`), attacks on it end as done and fetches or
follows as failed.

## Squads and formations

A `move`, `regroup`, or `stay` at a place gives each unit its own slot in
a formation: `wedge` (default), `line`, `column` or `circle`, facing the
way the squad travels unless a facing is given (drag while giving the
order). Slots are handed out with the Hungarian method, so the squad
travels the least in total and no two paths cross.

Selection works like every real-time strategy game: click or tap one,
drag a box, Shift adds or removes, Ctrl+1..9 stores a group and 1..9
recalls it.

## What a click means

Pointing at something with units selected gives the one obvious order
(`contextOrder`):

| Pointing at | Order | With the force modifier (Ctrl, a held button) |
|---|---|---|
| an enemy | attack | focus fire |
| an item | fetch (units that can't fetch walk there) | |
| the player, a friend, another unit | follow them | |
| the ground | go there | hold position there |

## Controls

Every order can be given with a mouse and keyboard, a controller or a
finger, and all of them are rebindable actions (`docs/INPUT.md`).

- **Mouse and keyboard**: left click selects, drag selects a box, right
  click gives the context order, keys give the rest (each demo's README).
- **Controller**: the reticle in the middle of the view is the pointer.
  A gives the context order, the D-pad gives the four most used orders,
  and holding LB opens the **radial wheel** with every order: tilt the
  right stick to pick, let go of LB to give it. Back in the middle keeps
  the pick, so a flick and a release is enough; B closes the wheel.
- **Touch**: tap a unit to select, tap the ground or a thing for the
  context order, hold a finger to open the wheel under it (drag to pick,
  let go to give, back to the middle to cancel), and the big picture
  buttons along the bottom give an order with one tap.

## Reading the player (companions)

A good companion acts before it is told. `IntentReader` watches the
player's own movement and view and reports what they seem to be doing:
standing a while (`idle`), walking, running (with where they will be in a
moment), looking at something for a moment (`looking`), or walking up to
something (`approaching`, e.g. the pet itself: it wants petting). The
brain decides what to do with it.

`followSlot` says where a follower should be so it never blocks the
player: to one side and a little behind, on the side it is already on (so
it never crosses in front), further aside the faster the player goes.
`inLeadersWay` says whether a spot is on the player's path.

## One set of blocks, three levels

Orders are play blocks (`docs/PLAY_TO_MAKE.md`):

- **Simple**: the picture buttons (Come, Sit, Stay, Fetch, Go, Attack) and
  tapping in the world.
- **Nodes**: every order is a node in the Orders section ("Send to",
  "Follow", "Focus fire", ...), plus the events *When given an order* and
  *When an order is done*. Left unconnected, "Who" is whoever is selected,
  or the thing itself in a thing's own graph.
- **Lua**:

```lua
order.move({ a, b, c }, Vec(10, 0, -4), "line")
order.focus(order.selected(), tank)
order.fetch(dog, ball)
hook.Add("OrderDone", "cheer", function(e)
    if e.order == "fetch" and e.ok then play.say("Good dog!") end
end)
```

Events queue while a frame runs and fire once a frame
(`OrderScript::fireEvents`), so an order given from a hook never re-enters
the script that gave it.

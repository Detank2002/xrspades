# Changes

What each release brings. The release workflow puts a version's section at
the top of its release notes, under "What's new", and the game shows those
lines when it offers the update. Keep each line short: one change, in a
player's words.

The first six lines of a version are its headline: the six biggest things a
player would care about, each at most 90 characters (the release workflow
checks). Older versions of the game show only those six in their update
prompt; newer ones show them and a See all changes button for the rest.

## 0.5.0

- New: ZeroSpades X is now XRSpades, same game, new name
- New: on Quest, Foveated Rendering, on by default, for a smoother game (Settings, VR)
- New: pick a skin for each weapon in game, with a picture of each (Esc, Mods, Skins)
- New: in VR, put the controllers down and use your hands in the menus
- New: bullets and the spade chip real holes out of blocks
- New: in VR, watch a game or a demo as a model on a table you can grab and turn
- New: the menu's links take you to XRSpades' releases, code and Discord
- New: in VR, getting hit kicks the controller on the side it came from, harder for bigger hits, and reddens the edge of your view
- New: in VR, a heartbeat at the edge of your view when your health is low
- New: in VR, the keyboard stands on its own in front of you, and you can grab it with the grip and put it where you like
- Improved: the VR keyboard has caps lock (Shift twice), arrow keys to fix typos, a Close key, and Back repeats while held
- Improved: in VR, the spade is solid and hits where its tip strikes a block, with a flick instead of a full swing
- Improved: in VR, reloading by hand takes the magazine anywhere under the front of the gun, without twisting your wrist
- New: in VR, change what every controller button does, on a picture of your controllers (Controls, Change buttons), and swap the sticks
- Improved: the button-changing screen shows each button in its own colour, by its real name, and lights it up as you point at its choice
- New: in VR, your grenades and blocks hang on your hips where you grab them, fewer as you use them up
- New: in VR, feel blocks break and set under your hand, every hit you land, and grenades going off near you; the shotgun kicks harder
- Improved: 3D sound in headphones is on by default (Options, Audio to change it)
- Improved: the faster Vulkan renderer is the default; a PC that can't run it starts with OpenGL
- New: search in Settings: type a word and every tab shows only what matches
- New: in VR, a first-time setup for your hand, standing or seated, turning and the comfort vignette
- Improved: in VR, the main menu stands you in one of the real maps the flat menu flies round
- Improved: the main menu says Singleplayer again
- New: a Weapon page in the Escape menu to change sights and aiming without digging through Settings
- Improved: in VR, the rifle uses its open iron sights by default; the scope is one click away on the Weapon page
- Improved: in VR, grabbing from the holsters is far more forgiving: bigger zones, a squeeze a moment early or late still counts, the hips sit at your own height, and looking aside no longer moves them
- New: Settings has Simple and Advanced: Simple shows the common settings for the screen you play on, Advanced shows everything
- New: an Audio tab with every volume, and an Advanced tab for debug and technical settings; graphics settings that only the quality preset could reach have their own rows
- New: a scope type that uses your crosshair while aiming
- Changed: mouse acceleration off and muzzle flashes on by default; games are recorded by default (the newest 10 kept)
- Improved: in VR, spectating and demos start on the table view (Settings, VR to turn it off)
- Improved: in VR, the Cross sight is now a ring reticle that tilts with the gun, and the sight's frame hides only the part of it behind it
- Improved: in VR, Quest headsets have a little more headroom for keeping a steady frame rate
- Improved: in VR on Quest, each frame goes straight to the headset, a little faster with or without foveated rendering
- Improved: a sharper picture on a monitor: High draws the game at 125% and Ultra at 150% instead of blurring it with FXAA, and Render Scale goes up to 200%
- Fixed: in VR, striking a block with the spade digs it again
- Changed: the spade stays a spade when you hit with it, instead of turning into a pickaxe
- Improved: in VR, tips and in-game popups float in front of you and catch up when you look away, instead of being stuck to your face; the training tips are smaller
- Fixed: in VR, the menu behind the first-time setup no longer takes clicks
- New: in VR, walk and turn around the main menu's map with the sticks; the menu floats along with you
- Fixed: in VR, a team ping off to the side shows in both eyes, at the edge of where you look
- Fixed: in VR, joining as a spectator opens the table view, as it should by default
- New: in VR, your hands are shown in the table view
- Fixed: in VR, the table view no longer shows a patch of water under you that moved with you
- Fixed: in VR, the keyboard comes up under the menu and moves with it, instead of being left wherever you were looking when it opened
- Fixed: on Quest, graphics too heavy for the headset no longer leave you stuck: if the game cannot start or runs far too slowly, it goes back to the Performance level and tells you
- Changed: on Quest, the graphics are set with Headset Quality in the VR tab; the PC's Low to Ultra are not offered there
- New: in VR, choose the headset's refresh rate (Settings, VR, Refresh Rate): a Quest 3 holds 90 and 120 Hz
- Changed: on Quest, settings for a monitor (such as Render Scale) are no longer shown; Eye Resolution in the VR tab sets the headset's sharpness
- Fixed: bullet holes near a block's edge no longer flicker dark (most visible in VR)
- Improved: bullet and spade holes can be cut into any face of a block, several on one block, its edges always left whole
- Fixed: blocks with a bullet hole no longer flicker at their edges or show a thin line of sky
- Fixed: bits of debris and flesh lie still on the ground instead of shaking
- Fixed: in VR, shots land where the sights and the laser point again, at any range
- Improved: in VR, your height is reset by itself when you put the headset back on
- Changed: a headshot bursts the head only now and then (one in twenty)
- Changed: in VR, the belt is stepped like the rest of the game's voxels
- Fixed: in VR, the grenades and blocks sit lower at the hips, digging straight down no longer grabs them, and the grenades on the belt no longer overlap
- Fixed: in VR, the reflex sight's Cross is cut off by the sight's frame where the frame really is
- Fixed: in VR, aiming with the stock while looking steeply down no longer swings the gun as you nod
- Fixed: in VR, the table view keeps a smooth frame rate on Quest: the map on it is a simpler, blockier copy
- New: Edit Mode, for map-making servers: unlimited blocks, build anywhere, even in the air
- Fixed: grenades no longer fill the air with big grey squares; the debris is small and the colour of what blew up
- Improved: the newest ZeroSpades changes, including sharper windows on scaled Windows displays and smoother tool animations
- Improved: in VR, a new map stays hidden for a moment until the frame rate settles, instead of juddering while it loads
- New: in VR, a Hip Height slider moves the grenades and blocks on your belt up or down
- New: in VR, a body mirror beside the menu shows you your body, belt and shoulder tools as you move, so you can see what the body settings change (Settings, VR)
- Fixed: in VR, dying and respawning no longer darken just the top of your view
- Fixed: chipped blocks no longer leave gaps you can see through at their edges; edges open to the air break off instead
- Changed: in VR, the grenades and blocks sit lower on your hips (Hip Height to move them)
- Fixed: in VR, a line of blocks drawn quickly starts where you began it
- Changed: a player's body is removed when they respawn, so heavy fighting keeps a steady frame rate (Options, Effects to change it, and how many bodies stay)
- New: star your favourite mods to keep them at the top of the mod lists
- New: the main menu's mod list shows a picture of each mod's weapon
- New: the server browser shows every known server list at once, with a filter to pick one
- Improved: the minimap darkens the area outside the map
- Improved: a new spade digging animation
- Fixed: the last settings in a Settings tab can be scrolled to again
- Fixed: in VR, the menu stays still while you turn to look at the body mirror
- Fixed: in VR, the settings menu and the scope keep a steadier frame rate
- Fixed: on Quest 2, foveated rendering no longer crashes, with or without colour correction
- New: High Top foveated rendering, which keeps more detail above the middle of your view
- Changed: foveated rendering stays at the level you set instead of changing with load, which some found made them feel sick
- Improved: placing and breaking blocks no longer makes the game stutter
- Fixed: lines at the edges of the view in a headset
- Fixed: a block standing on its own could lose its corners to gunfire
- Improved: 2x anti-aliasing for headsets, a fifth faster than 4x; the Performance level uses it
- Fixed: a thin bright line along the shore where land meets the water
- Improved: other VR players' heads and hands move smoothly instead of in small steps
- Fixed: other VR players' arms no longer stick up in front of their face when they dig or throw
- Improved: in VR, Color Correction says it is not used while Foveated Rendering is on, instead of doing nothing
- Fixed: in VR, a setup that had foveated rendering shifting its detail with the load now stays steady
- Fixed: in VR on Quest, the sky and far blocks are no longer black with Volumetric Fog on

## 0.3.6

- New: a headshot kill bursts the skull into pieces (Options, Effects to turn it off)
- Improved: fewer and smaller particles on the Quest
- Fixed: zombies' right arms and legs, and one zombie's chest
- New: zombies hold their own pickaxe as well as their spade

## 0.3.5

- New: zombies come in several looks, and each kind of brute has its own
- New: zombies and brutes carry their own spade, and the Spitter its acid
- New: smoke clouds and coloured smoke markers on servers that use them
- New: a model editor, sharper text on high-DPI screens, and automatic screen setup
- New: Escape on the main screen asks before quitting

## 0.3.4

- Improved: smoothness on servers with up to 200 players
- Fixed: flashlight disabled on some graphics settings
- Improved: cleaner colours, with the colour filter off on every graphics setting
- New: icons in the server list for each server's extra features; hover a server to see what they are
- New: an Extensions filter in the server list
- Fixed: some server features named "Unknown" in the pause menu

## 0.3.3

- Training has its own page: the Tutorial and Aim training
- Aim training: Gridshot, Flick, Tracking, Strafe duel and Long range, with scores and your bests
- The tutorial shows where to go next, with a checklist and a timer
- Ammo, grenades and blocks never run out in Training
- VR players crouch instead of folding over, with thicker arms and hands that turn
- VR: the kill feed is back, in front of you
- VR: block colours can be picked on the wrist menu again
- A mod that breaks the scripts is turned off, so it can always be unticked
- The update prompt shows what the new version brings

## 0.3.2

- Fixed a crash on Mac when a mod's sound could not be read
- A sound that cannot be loaded now plays as silence instead of breaking a weapon

## 0.3.1

- The game updates itself on Windows and Linux: click Update Now
- Linux: the package now includes the game's data

## 0.3.0

- First release of ZeroSpades X: VR on PC and Meta Quest 3, and Training

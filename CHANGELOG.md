# Changelog

All notable changes to Kennel.gg Wardogs Streaming Tool. Release notes on GitHub are taken from here.

## 0.19.6
- **Every kill is written down with what made it and exactly when.** Each kill ClipHound logs now carries the weapon, its type (gun, grenade, explosive, launcher, mortar, artillery, bow, melee, vehicle, vehicle weapon), the vehicle when there was one, a headshot flag, and its time to the millisecond.
  - **Guns** are named by the game's name only when something named them (your HUD at your kill, or a learned kill-feed icon); otherwise the weapon is left blank rather than guessed.
  - **Grenades, C4, rockets and mortar rounds** are named after the explosive, or "Grenade", "C4", "Rocket", "Mortar" - never a gun.
  - **Vehicle kills** are named after the mounted gun when you were on one ("M249 Machine Gun", with the Humvee as the vehicle), otherwise after the vehicle if your HUD named it, otherwise "Helicopter", "Tank", "Vehicle" or "Artillery" - never the gun you last held on foot. Someone else's vehicle or explosive kill is never given a gun's name either.
- **The files.** In the library folder, index.csv gains the weapons, weapon types, vehicles, and every kill's time (as seconds before the end of the clip and as an exact clock time); a new kills.csv has one row per kill with all of it. The plugin's clips.csv gains the same weapons and per-kill times, and the .json next to each clip gives every kill its seconds-before-the-end. An older index.csv is brought up to the new columns in place, nothing lost. Titles with commas no longer split clips.csv into extra columns.
- **Kennel Cut 1.6.8** reads the per-kill weapons for the highlight montage: each kill's name, type and vehicle come from ClipHound's live log, and its own reading of the video fills in only gun kills nothing named. A clip with a grenade in it no longer loses the gun names of its other kills.
- Titles name every weapon of a multi-kill in order: "with the Galil and a grenade".

## 0.19.5
- **Grenade kills are no longer named after the gun in your hands.** A grenade goes off after you have switched back to your rifle, so the HUD shows the rifle by the time it kills. When the kill feed shows a grenade, C4, a rocket or a mortar round (or a bare explosion), the kill is named after the explosive the HUD showed in your hands in the 15 seconds before ("with the M67 Frag Grenade"), and never after the gun held now; with none, it keeps the kill-feed type ("with a grenade"). Clip titles, tags and the highlights reel follow, so a grenade kill no longer scores or reads as a rifle kill.
- The other way round too: a gun kill made just as you pull a grenade is named after the gun you were holding a moment before.

## 0.19.4
- **Fixed: magazine packing stayed off after Auto switch was turned back on.** ClipHound only watches for the inventory screen while Auto switch is on, and turning Auto switch on or off did not tell it: it stayed off until OBS was restarted. It is told at once now.
- **ClipHound, in words, on the dock.** A line under the clip buttons says whether ClipHound is running (with a Start link when it is not), the weapon it sees in your hands, and whether it is watching for mag packing - or that mag packing is waiting for Auto switch, with a link that turns it on.
- **Mag packing has its own tick on the dock,** next to Auto switch on the On screen heading, so it can be seen and changed without opening Settings.

## 0.19.3
- **Weapon names are read in all 14 of the game's languages.** English, German, French, Spanish, Italian, Portuguese, Polish, Turkish, Russian, Ukrainian, Japanese, Korean and Simplified and Traditional Chinese. The translated names come from the game's own text (bows, grenades, explosives, hammers, the mortar, and the medkit and tools that mean the gun is put away), and every gun is also recognised by its model designation, which the game keeps in every language: "FUSIL AK74", "АК-74", "M67 破片手榴弹". Russian's Cyrillic look-alike letters (М4, МК22) and transliterations (РПГ-7, СКС) are handled. Latin-script languages are read with the model ClipHound ships; Russian, Ukrainian, Japanese, Korean and Chinese fetch their own reading model once, a few megabytes, the first time the game is set to them, so the installer does not grow. Tested on 123 plates across the 14 languages at three text sizes: 121 read from a single frame, none named wrongly, and no name was ever read off scenery.
- **Game language (Settings, General, and Setup) lists all 14 languages.** Pick yours and the weapon names are read in it. The downed screen is still recognised in English, Spanish and French; in another language every known wording is searched, and the dock asks for a frame to add yours.
- OCR tends to drop the space before the calibre ("M45.56" for the M4 in 5.56): the calibre is split off before matching.

## 0.19.2
- **The kill feed says a kill happened; your HUD says which gun.** Multi-kills, distances and deaths still come from the kill feed. For your own kills the weapon now comes from the HUD every time, and the kill-feed icon no longer overrides it. The one exception is a kill the feed shows as a vehicle's (a chopper, a tank): the gun you last held did not make that one.
- **It always knows what you are holding.** The item plate in the bottom-right is watched the whole time, and every weapon it shows is remembered with the moment you switched to it. A kill takes the last one, so it is named even when the plate has faded by the time you shoot. A crop that has not changed is not read again, and at most one read a second is made, so it costs next to nothing. Switching to a medkit or a tool (Emergency Resuscitator, smoke grenades, drills, the wrench) counts as putting the gun away; unreadable text never does.
- A kill-feed row with scenery behind the plate text no longer hides the name: it can start anywhere on the line.
- A multi-kill made with more than one gun names them all: "Triple kill ... with the Galil and the M1911".

## 0.19.1
- **Pick who you are playing with.** The dock used to offer every squad mate you had ever added. The Squad window now has a **Playing** tick for each: outside a Kennel.gg voice channel the dock offers only the ticked ones, and inside one it offers whoever is live in it, by itself. A squad mate whose pop-out is open is ticked for you. Two buttons: "Playing: whoever is live" and "New session" (untick everyone). Stream Deck cycling and "hey kennel, change" go through the same people. On this update, Discord squad mates start unticked and tick themselves as their pop-outs open.
- **Kills named by the game's own weapon names.** ClipHound now reads the item plate in the bottom-right of your HUD (a small crop, twice a second) and names your kills with what you were holding: "Double kill at 68m and 61m with the Galil", tags "galil" and "assault-rifle". Each of those kills also teaches it that weapon's kill-feed icon, so the same icon on someone else's row (the kill that downed you, a squad mate's kill) is named too. Nothing is named from a generic shape: until a weapon has been seen in your hands, its kills keep the class ("rifle"). Where two guns turn out to draw the same icon, the title says both ("A-91 or T-21"). The name list is the Early Access one from the game files, 34 guns plus grenades, explosives, tools, emplacements and vehicle weapons.
- Fixed: on a 1440p feed, the kill-feed rows ClipHound keeps for learning were cut from the wrong height and held only background.
- Fixed: the Squad window's monitor choice could save the wrong monitor.

## 0.19.0
A new dock, Setup and Settings, built around what streamers actually ran into this month.

- **The dock says what is wrong, and fixes it.** A health strip under the status pill: Game, Scene, ClipHound, Discord, Replay and Voice, each a dot that goes amber or red. Hover for the reason, click for the fix: start ClipHound, start the replay buffer, switch to the plugin's scene or make the live one the plugin's, detect your Discord username, pick another mic.
- **No more pop-up windows over OBS.** Everything that used to open a window by itself is now a message in the dock with its buttons: the game language not recognised (with Save a frame), a minimised pop-out whose picture froze (with Restore), an old copy of the plugin still installed (with Open its folder), Closest pressed without ClipHound (with Start ClipHound), a newer build, the support note. Setup only opens by itself when you are not streaming or recording; otherwise the dock says it is not finished.
- **One row of people per view.** On screen: Me, each squad mate, and Closest; the one who goes up when you are downed has a dashed edge, the one on stream is lit. Dual POV: Off or a squad mate. Auto switch and Auto in vehicles sit on the headings. This replaces the drop-down, Show friend's POV / Back to me, and Force Dual POV.
- **Voice shows what it hears.** While voice control is on, the dock has a mic level bar and the last thing it heard with what came of it: a command, "not a command", or the wake phrase waiting for one.
- **Clips row:** Save clip, Instant replay and Highlights side by side, and the last clip with Rename and Replay links.
- **Add pop-outs no longer asks for each in-game name in a row of windows.** The dock reminds you to mute the new pop-outs in Discord; the Squad window has an In-game name column you type straight into, and when Closest is on the dock asks you to check them.
- Squad, Settings, Setup, Logs, Clips, Start/Stop ClipHound, the Kennel.gg Discord and a **compact mode** (always, or only while streaming or recording) are in the ⋯ menu.
- **Setup:** the first page asks your name as the kill feed shows it (clips need it) and the game language. The squad page is three short steps, and says plainly that squad automation is for members of the Kennel.gg Discord (it used to tell you to add the bot to your own server, which it now leaves). The clips page sets the replay length. The last page checks everything live, with a fix button for anything red, and offers voice control, the vertical canvas and the Stream Deck plugin.
- **Settings by task:** General (your names, your game and scene, the language, ClipHound), Squad & POV, Dual POV, Clips & replays, Vertical (beta), Voice (beta), Stream look, Advanced (downed detection, the ClipHound connection, the kill-feed area), Logs and Help. The game source is set in one place; Help lists every way to drive the tool.
- Every message that named an old tab points at the new one, and "squad mate" replaces "friend" everywhere.
- Fixed: the Squad window's "Parked on monitor" choice could save the wrong monitor.

## 0.18.18
- **No more "buffering" as a replay starts.** The replay used to appear the moment its file opened: the viewer saw the first frames of the file, then the jump to the moment, then a stall while the decoder worked forward from the keyframe before it. The file now opens and seeks while it is still hidden, and the replay appears only once playback has reached the moment, on both canvases. If it has not got there after a moment and a half it is shown anyway.
- Settings, Clips: "Decode replays on the GPU", off by default, for a PC where the software decoder cannot keep up with a 1440p60 recording.
- **Play highlights on the vertical scene plays a vertical compilation.** When the vertical scene is on, ClipHound also builds a portrait compilation from the clips' vertical twins (the same moments, cut to the same windows, portrait cards), named "... [vertical]" next to the landscape one, and the vertical scene plays that. With no vertical twins yet, the vertical scene gets the landscape one as before and the log says so.

## 0.18.17
- **OBS could stay running after you closed it.** OBS releases every source on the way out and then tears the video down; the plugin was still holding its dual-POV scene (the browser page and the squad's window captures inside it) and the microphone tap at that point, which OBS logged at every close as "Not all sources were cleared when clearing scene data". With a Backtrack output still running when you closed, the teardown that followed could hang: OBS never reached the point of unloading its modules and had to be ended from Task Manager. The plugin now lets go of everything it holds the moment OBS starts clearing the scene collection, before any source is released, and picks it all up again after a scene-collection change.
- At exit the plugin no longer spins OBS's event loop while it waits for ClipHound to close, cancels its web requests (live checks, the roster) and waits for their threads, and waits for its own worker threads by counting them rather than by a flag that could not clear in time.
- The installer removes a copy of the plugin left under its first name (POVBridge), which otherwise loads next to this one, and the log says so if one is found.

## 0.18.16
- **The vertical swap was being covered by the game.** The vertical "Always on top here" guess ticked a capture card as a camera, so every swap on the vertical scene was lifted under the game feed. The game source can no longer be in either on-top list and is taken out of existing settings once. Also, when the vertical scene carries the same name as the main scene ("GAMING" on both), the fallback lookup could land on the main canvas's copy; it now never resolves to the main canvas.
- The log says which scene, on which canvas and at what size, the vertical swap resolved to, when a squad mate's feed is shown there, and any error in plain words.

## 0.18.15
- **A dock or hotkey clip takes the last sentence you said.** It used to be titled from a fixed window, eight seconds before the press to four after, which often caught half of two sentences. The listener now marks where each sentence ends, and a press takes the last one you finished; if you were mid-sentence when you pressed, or start one within a moment of pressing, it waits for that sentence to end (up to four seconds) and uses it. Nothing said in the last twenty seconds and the old window applies. "PV" comes out as "POV" in titles. Naming now works with voice commands switched off, as the tick box always said.

## 0.18.14
- **The vertical scene replays the vertical clip.** With Aitum's vertical Backtrack on, a clip produces a portrait file too, in the Vertical folder. The plugin used to name only the first file it found and leave the other; it now names both, keeps the portrait one as the clip's vertical companion (noted in the .json), and the instant replay on the vertical scene plays that file full height, seeked and stopped in step with the horizontal one, instead of a cropped copy of the horizontal clip. Without a vertical file it shows the horizontal clip there as before.
- **Clip + replay waits two seconds** for the file to be closed properly before playing it, and with a vertical canvas up to three seconds more for the vertical file to land and be paired.

## 0.18.13
- The Dual POV crew-mate list on the Dual tab follows the same rule as the dock's pickers: in a Kennel.gg voice channel it lists only the people live in it; anywhere else it lists the whole squad, with a dot on the ones known to be streaming. The one already chosen stays in the list whatever its state. (The dock's own Dual POV picker already worked this way.)

## 0.18.12
- **Voice (beta): quicker again, without the old jump.** The hold on a command that a longer phrase begins with ("clip", "replay", "dual") is 1.1 s and now starts from the first partial result instead of after the listener's own wait for silence, so it lands about 1.2 s after the last word instead of 1.8 s; a command nothing longer begins with lands in under a second, and "clip and replay" said in one breath fires before the sentence is over. A pause of up to about half a second between "clip" and "and replay" is bridged; a longer one is two asks.
- Whisper's second look at the wake phrase is re-taken at the end of the sentence when its first look at a half-sentence said no, so a command is not lost to one bad glance.

## 0.18.11
- **Voice (beta) tuned on a real streamer's clip.** Of four asks in the clip only "replay" got through; all four do now. Whisper's second look accepts the ways it spells "kennel" in different accents ("kettle", "kernel", "kendall"...) as long as the listener heard the wake phrase and a "hey" came first, so "dog kennel" still stays quiet. "P O V" said as letters counts as "POV". "Both POV" and "picture in picture" mean Dual POV; "switch POV", "swap POV" and a bare "switch" or "next" mean the next squad mate. A different command straight after another is no longer swallowed as a repeat.
- **A breath in the middle of an ask.** "Hey kennel, clip ... and replay" used to save a plain clip the moment "clip" was heard. A command that a longer phrase begins with (clip, dual, replay) is now held for about a second and a half, and words that follow complete it: the hold stretches while you are still talking and "and replay" turns it into clip + replay. Said in one breath it is as quick as before; said alone it fires when the hold ends.
- **Voice settings could be lost at start-up.** The plugin sent them 1.5 s after ClipHound connected, and on a slow start ClipHound's voice module was not up yet, so they were dropped: the microphone flowed and nothing listened. ClipHound now keeps settings that arrive early and asks for them again when it is ready.
- **A second ClipHound is called out.** The plugin only ever knew about the copy it started; one started another way, or an old copy left running, answered instead and nothing matched the logs. Now ClipHound reports its version on connect (the plugin logs a mismatch and what to do), a copy that finds another already running writes why it exits, and a connect that comes too soon after the plugin's own launch is flagged as a copy that was already there.

## 0.18.10
- The dock's events list says why a squad mate went on: INVENTORY, DOWNED, VOICE, STREAM DECK, CLOSEST or SHOWING, and "back up" names what ended it. It said DOWNED for all of them.

## 0.18.9
- **Inventory switching comes back quicker.** The reader looked once a second and needed two misses, so closing the inventory took two to three seconds to reach the stream, and any stray read stretched it further. It now gets its own two small crops three times a second (the hint and the tab, a few kilobytes), swaps after the screen has been up for two seconds, and comes back to your POV on the first crop without it, a third of a second after it closes. The log says what it read at each open and close.

## 0.18.8
- **Magazine packing / inventory POV switching.** Repacking magazines means standing still with the inventory screen open, and a stream of that is a stream of nothing. ClipHound now reads the "COMBINE AMMO" hint on that screen (and the INVENTORY tab as a second sign) once a second; after two seconds of it a squad mate's POV goes on stream, the chosen one or any live one, and yours comes back the moment the screen closes. Going down while it is open hands over to the downed swap as usual. On by default; the tick is on Settings, Switch, under the extras. Needs ClipHound running.

## 0.18.7
- **Vertical canvas (beta): your camera and alerts stay on top there too.** The vertical scene has its own "Always on top here" list under the vertical settings, guessed from that scene's camera and alert sources once and yours to tick; they are lifted back over the squad mate, the overlay and the replay every time, as on the main scene. With nothing ticked, the main list's names are used if the vertical scene has them.
- **The portrait POV tag can sit lower.** "POV tag height" next to it, a share of the canvas from the top; 22 % by default, down from the old spot near the top edge, so it clears a camera or a title up there.

## 0.18.6
- **"Clip and replay" no longer turns into a plain clip.** The listener acts on what it has heard so far, and "clip" on its own is a complete command, so it fired before "and replay" arrived. A command that is the start of a longer one ("clip", "dual", "replay") now waits for the end of the sentence; commands that no longer phrase begins with still fire the moment they are said.

## 0.18.5
- **No more crash when OBS closes.** OBS destroys its main window, and the plugin's engine with it, before it unloads plugins; the unload then told the dead engine to stop and OBS fell over inside Qt (crash logs ended in Engine::stop under obs_module_unload). The unload now only stops an engine that is still alive, stopping twice is a no-op, and shutdown also waits for the frame worker thread, so a poll in flight cannot touch the engine after it is gone.

## 0.18.4
- **Stream Deck plugin.** A separate download, `com.kennelgg.wardogs.streamDeckPlugin` (double-click to install, Stream Deck 6.4 or newer), with eight keys: squad mate POV (a chosen squad mate, or "whoever is live" in Kennel.gg voice; press again to come back), next squad mate, clip, instant replay, clip + replay, voice control on/off, Dual POV on/off, and my POV. The keys follow the OBS plugin: a dot marks who is live, on/off keys light up, and they say "OBS?" when the plugin is not running. It talks to the plugin over the same local bridge ClipHound uses; nothing leaves the PC.
- The bridge tells a controller apart from ClipHound, so a Stream Deck connecting or leaving never stops the kill-feed frames or the microphone.
- Voice (beta): a soft or swallowed "hey" no longer loses the command. "[unk] kennel replay" from the listener, or "okay kennel", "a kennel", "hi kennel" from Whisper, all count as the wake phrase; "dog kennel" still does not.

## 0.18.3
- **Every voice command, and every way to say it,** in a window from the Voice tab ("All voice commands and the ways to say them..."): ten commands, each with its phrasings, what it does and which switch it lives under. The list is taken from the listener itself, so it is what actually works.

## 0.18.2
- **Tones after a command.** A short rising pair when a command was taken, a low falling pair when what followed "hey kennel" made no command, so you know without looking. Same device and volume as the chime; its own tick on the Voice tab.
- A one-word ask has to lead: "me" buried in a misheard sentence no longer passes for "back to me"; the sentence gets the falling tone instead.

## 0.18.1
- **"Dog kennel" no longer wakes it.** "Kennel" on its own used to count as the wake phrase; now both words are needed, "hey kennel", in what the listener hears and in Whisper's second look. Tested: "dog kennel", "the dog kennel is over there", "put it in the kennel" and a bare "kennel replay" all stay quiet; "hey kennel" and "hey kennel replay" fire.

## 0.18.0
- **Much less CPU.** The plugin was rendering the whole game frame at its native size, reading it back from the GPU, JPEG-encoding it and handing it to ClipHound, which decoded it, ten times a second - fourteen megabytes a frame at 1440p - and ClipHound then cut the kill feed out of it. Now the plugin renders and sends only the kill-feed crop at the reading rate, a few hundred kilobytes a second, and the whole frame once a second for the minimap team check, the NEARBY panel and the vehicle list, which are read once per frame instead of ten times. On the PC that reported 20 %, most of that was this.
- **The encodes are off unless chosen.** The end-of-stream compilation (segments cut and encoded after every clip while you play) and run merging (clips re-encoded into one) were on for everyone. They are now off by default and switched off once for existing installs; tick them on the Clips tab if you want them. Clip trimming stays on: it is a straight cut, no encode.

## 0.17.6
- **"Hey kennel, clip replay."** One ask, two things: the clip is saved and, the moment the file lands, played back as the instant replay. "Clip and replay", "save and replay" and "clip that and replay" all count. Needs both the clip and the instant replay commands on.

## 0.17.5
- **Your replay buffer length is yours.** The plugin used to write its own 45 s into OBS's replay-buffer setting at every start, over whatever you had chosen. It now reads OBS's length and follows it. The Clip length box on the Clips tab shows OBS's value; change it there and it is written into OBS once, restarting the buffer if it is running. Change it in OBS and the plugin picks it up at the next start. 45 s is what Sombrero uses, not a rule.

## 0.17.4
- **The chime plays through one device.** It was going out through the PC's speakers and into OBS's mix at the same time, and with desktop audio captured the recording got it twice, a beat apart. A choice next to the volume on the Voice tab: through this PC's speakers (the default: you hear it, and a captured desktop puts that one copy on the stream), into the stream through OBS (straight into the mix, heard by you only if you monitor OBS), or both, for a desktop that is not captured.

## 0.17.3
- **"Hey kennel" no longer fires on "get to the".** Told what can be said, the listener will hear the wake phrase in anything close to it. Now Whisper gets a second look at the last two seconds and has to hear "kennel" too before the chime or a command goes through. On a test set of look-alikes ("get to the", "hey can you", "take another", "he can help") none got through, and every real "hey kennel" did.
- **Dual POV no longer forces itself on at start-up.** The Dual tab's tick used to follow the window being up, so a settings save while it was up (from the dock, a voice command or the vehicle detector) made it come back forced at every OBS start. The tick now means one thing, "up from the moment OBS starts", and it is cleared once for everyone; the window still comes up whenever you ask.
- **The dock lists the whole squad when you are not in Kennel.gg voice.** Only in a Kennel.gg voice channel does it narrow to the people live there. Outside, everyone is listed, with a dot on the ones known to be streaming; "nobody streaming" is gone.

## 0.17.2
- The "hey kennel" chime plays on the stream as well as on your PC: the plugin drops the same two notes into the mix through a small media source in your scene, so viewers hear that a command is coming. One tick on the Voice tab turns both off, and a volume next to it sets how loud, on the PC and the stream alike.

## 0.17.1
- **Vertical canvas (beta): the whole squad comes along.** Switching it on, or changing the squad, puts every squad mate's source into the vertical scene straight away, full height and centred, hidden until they are shown; the look overlay in its portrait form and the instant replay with its frame were already there. Dual POV stays on the main canvas.
- **Add a squad mate from the Squad window, any way.** Under the Discord pop-out button there is now "Add a squad mate another way": Twitch, Kick, YouTube, VDO.Ninja or an OBS source, the same window as Settings, Switch, Add.
- **A chime after "hey kennel".** A soft two-note chime through your PC's speakers (never the stream) a moment after the wake phrase, and the next few seconds of words are the command: "hey kennel", chime, "replay". Saying it in one breath still works. Off with one tick on the Voice tab.
- **Kennel.gg Discord only.** The Squad window's server picker and the "add the bot to your server" link are gone: the squad automation is a Kennel.gg thing, the roster lists that server alone, and the bot leaves any other server it is added to.

## 0.17.0
- **Vertical canvas (beta), off by default.** Settings, Switch, a new group: tick it and pick the scene your portrait stream shows, and the POV swap happens there as well (the squad mate's feed full-height, sides cropped, the look overlay in its portrait form), and the instant replay plays there too, full width and centred with its frame. The same sources as the main canvas, so nothing is decoded twice.
- The vertical scene picker now lists the scenes of OBS's own extra canvases, as Aitum Stream Suite makes them ("canvas / scene"), which the old picker could not see: OBS keeps each canvas's scenes apart from the main list, and the plugin now looks canvas by canvas, both to find the scene and to hide a feed everywhere when you come back up. An install that had a vertical scene set keeps it on.

## 0.16.5
- **"Hey kennel" is the wake phrase.** Two words are much clearer to the listener than one, and it is what people say anyway: "hey kennel, replay", "hey kennel, clip that, he fell off the roof". "Kennel" on its own still counts. An install that had the old "kennel" is moved to "hey kennel"; the field on the Voice tab takes any phrase.

## 0.16.4
- **Voice (beta) hears the commands now.** The first real session showed what went wrong: the small listener, left to guess any English, made "kendall replay" of "kennel replay" and "kennel club that" of "kennel clip that", and neither fired. It is now told exactly what can be said after the wake word (about ninety phrasings), so those come out as "kennel replay" and "kennel clip that", and ordinary talk comes out as nothing at all. Tested on synthesised speech: every command fired, and "what a nice shot bro" was ignored.
- **Names go through Whisper.** A squad mate's name is not a word the listener knows, so "kennel show bouga three four" is heard as "kennel show ..." and Whisper reads the name from the last few seconds: bouga34. Spoken numbers become digits.
- Half a sentence no longer fires: "kennel show my" used to be taken for "show me" before "squad mate" arrived. Only a whole phrase fires early; anything with a name waits for the end of the sentence.
- Clip titles anchor on "clip that" even when Whisper mishears the wake word ("then I'll clip that he fell off the roof" still gives "He Fell Off The Roof"), and Whisper is nudged to spell the wake word right.

## 0.16.3
- Voice (beta): the logs now show what is going on. The plugin says when microphone audio starts flowing to ClipHound; ClipHound says when the first audio arrives, reports the microphone level every two minutes (and calls out silence), and writes every sentence the listener makes of you, so a log shows why a command did or did not fire. The wake word is taken as heard, so "kennels" and "kenel" count as "kennel".

## 0.16.2
- **Voice control is a beta, off by default, English only.** The tab says so, and it stays off until you switch it on.
- **Each command on its own switch:** Kennel - instant replay; Kennel - clip that (with its own switch for "the sentence after becomes the file name"); Kennel - force dual point of view; Kennel - force squad mate point of view; Kennel - change squad mate point of view; Kennel - show closest squad mate point of view. Any of them can be turned off on the tab.
- **The words do not have to be exact.** What you say after the wake word is scored against many ways of asking for each thing: "play that back", "run it back" and "instant replay" all play the replay; "show my squad mate", "squad mate pov" and "show his pov" all force the squad mate's POV; "next squad mate" or "show bouga" changes it; "closest" or "who's closest" picks the nearest. Something that sounds like none of them is ignored; the log shows what was heard and how sure the match was.

## 0.16.1
- **One scene, chosen in setup.** The plugin's POV sources, label and overlay go into one scene and nowhere else, and Setup now asks which on the game page (the live scene is offered first). "The scene that is live" is gone from Settings: sources added to whichever scene happened to be up were the usual reason nothing showed on stream. An existing install keeps the scene that was live when it started, and the log says so. When the scene live in OBS is not the plugin's scene, the dock says so in red and names both.
- **"kennel clip that ... "** and then say what it was: the sentence after the ask becomes the clip's title, however much was said before it. A clip from the dock or the hotkey is still named from what was being said around the moment.

## 0.16.0
- **Your voice, first build.** A new Voice tab. Switch it on and your microphone's sound goes from OBS to ClipHound on this PC, where two small speech models run locally (downloaded once, about 120 MB; nothing is recorded and nothing leaves the PC).
- **Manual clips named by what you said.** Save a clip from the dock, the hotkey or by voice, and the words from about eight seconds before to four seconds after become its title: "Insane Triple Through Smoke - 2026-09-16 21-14-03.mp4". The whole sentence is kept in the clip's .json. Kill-feed clips keep their own names.
- **Voice commands**, after a wake word ("kennel" unless you change it): *kennel replay* plays the instant replay, *kennel clip* saves a clip, *kennel show bouga* puts that squad mate's POV on stream (names are matched loosely, spoken numbers work: "bouga three four"), *kennel back* returns to your own POV, *kennel dual* / *dual on* / *dual off* drive Dual POV, *kennel highlights* plays the compilation. The log shows every command as it was heard.
- **Typed titles and tags too.** The Save clip menu on the dock has "Save clip and add a note...": the clip is saved that instant and a small window asks for a title and tags while the game goes on (Enter, or just close it). And "Clips: titles and tags..." opens every clip in the folder, this stream's first, to name and tag them at the end of the stream. The file is renamed to the title and both go into the clip's .json.
- The microphone is the first Mic/Aux input in OBS unless you pick one; it is read whether it is muted in OBS or not, so push-to-talk streamers can still command it.

## 0.15.2
- The plugin's web requests can carry a method, a body and headers of their own; nothing changes for the update check or the roster.

## 0.15.1
- **"Game language not supported yet?"** With Game language on Auto, if the damage-log header keeps scoring close to the line without ever matching English, Spanish or French, a note says so once and offers to save a frame right then and open the Kennel.gg Discord, so a ticket with the picture is two clicks. It never shows once a wording has matched, when a language is picked by hand, or with a custom header cut.

## 0.15.0
- **The game in Spanish or French.** The downed detector looks for the damage-log header on your screen, and until now it only knew the English "VIEW DAMAGE LOG". A game in Spanish ("VER REGISTRO DE DAÑOS") or French ("AFFICHER LE JOURNAL DES DÉGÂTS") scored just under the line, so the swap fired once in a while and dropped after a few seconds. All three wordings ship now. Game language on the Detect tab is Auto: every wording is searched until one matches, the plugin logs which, keeps that one and remembers it for next time. Pick a language by hand if you would rather. The French wording was cut from a 1080p stream frame, the Spanish from a 1440p screenshot, so the Spanish one is the better tested of the two; if yours is not found, save a frame while downed (Detect tab) and open a ticket in the Kennel.gg Discord.
- **More languages by ticket.** English, Spanish and French are the ones on board. For another language, open a ticket in the Kennel.gg Discord with a frame saved while downed and it goes in the next build.
- The NEARBY reader skips the panel's own header in Spanish and French too (CERCA, À PROXIMITÉ), so it cannot be taken for a squad mate's name. Still English-only: the vehicle-seat reading (it reads the keybind list) and the "REVIVING" word that ends the swap a moment early; in other languages the swap ends when the damage log goes, as before.

## 0.14.4
- **No player name meant every kill was yours.** With the in-game name left blank on the ClipHound tab, every kill-feed row matched "me" on both sides, so ClipHound fired "Killed myself" on other people's kills every twenty seconds and asked for a clip each time. A blank name now matches nobody, and ClipHound's log says the name is missing.
- **The downed search reports near misses.** Once a minute, if the damage-log header scored close to the threshold without reaching it, the log says so and points to the Detect tab, where you can cut the header from your own screen. That is the case for a game running in another language: the built-in header is the English "VIEW DAMAGE LOG", and the Spanish "VER REGISTRO DE DAÑOS" only crosses the line now and then, which looks like a swap that fires once an hour and drops after a few seconds.

## 0.14.3
- **Pop-outs in any language.** Discord titles a popped-out stream in the client's own language, and only the English "x's Stream" was recognised: a French client's "Stream de bouga34" was reported as "no popped-out stream found" and the slot fell back to Discord's main window, which is why a squad mate's POV showed the Discord app instead of their game. French, German, Italian, Dutch, Spanish, Portuguese, Russian, Japanese, Korean and Chinese titles are read now, and a slot named by hand after the whole title matches its owner.
- **Playing on a server the bot is not in no longer counts as "nobody streaming".** If the Kennel.gg roster cannot see you in a voice channel, it stays out of the decision: pop-outs decide who is live, the swap fires, and the dock lists the whole squad, as it does without a roster. Only when the roster sees your channel does it rule someone out.

## 0.14.2
- **Renamed: Kennel.gg Wardogs Streaming Tool.** The OBS Project asks that plugins do not carry the letters OBS in their name, so that nothing looks official; "for OBS Studio" as a description is fine. Everything that showed the name changes: the dock, the Tools menu entry, the setup and settings windows, the installer and the uninstall entry, the website and the GitHub repository (the old address redirects). Nothing else moves: settings, sources, hotkeys and the update check carry on as they are.

## 0.14.1
- On the third start with the plugin, once, a note asks whether you are enjoying it and offers a link to support development. Maybe later closes it for good.

## 0.14.0
- **Clips are trimmed to the action.** Seconds after a clip lands, ClipHound cuts the 45 s file so it starts 10 s before the first kill it was made for (adjustable, Settings, Clips). It is a straight cut with no re-encode and no quality loss; the end of the file is untouched, so the offsets in the name and the sidecar stay right.
- **A run becomes one clip.** Clips made within the run window of each other (the "[1 of 3]" runs) are merged into one file of continuous action, "... [run of 3].mp4", next to the originals. Where two clips overlap, the seam is placed by lining their sound up rather than trusting the clock, so nothing repeats or skips. Re-encoded on the GPU at low priority.
- **Dead space out of a run**, off by default: with the option on, a merged run keeps a few seconds around each kill and drops any gap longer than the setting (12 s), which turns three 45 s clips into a minute and a half of nothing but the fights.

## 0.13.5
- The rule merge in 0.13.4 looked in the wrong place in config.yaml and added nothing; it now reads the detection section, so Fall damage and Killed myself reach an existing install.

## 0.13.4
- **Dying to fall damage is a highlight.** A death in the kill feed with nobody in the killer column is clipped as "Fall damage" (tags death, fall, funny), and one where the feed names you on both sides, with no explosive or vehicle icon, as "Killed myself". Both feed the replay and the compilation. Rules added in a build are now merged into an existing config.yaml, which used to keep the old list for ever.

## 0.13.3
- **The chat trigger word is yours to set.** Next to the cooldown on the Clips tab: what viewers type to play the replay, "!replay" by default. Case does not matter.

## 0.13.2
- **"!replay" in Twitch chat works.** ClipHound reads your Twitch chat through the same login the clips use, and a "!replay" from you, a moderator, a VIP or a subscriber plays the last highlight, once per cooldown. The login needs one more permission for this, so log out and back in to Twitch on the ClipHound tab once after installing.
- **The replay lands on the kill now.** 0.12.0 asked the media source to seek before the file had started playing, so the seek was dropped and some replays ran the whole clip from the start while others stopped after two seconds. The seek now waits for playback, is checked and retried, and the replay ends by the file's own clock. Hardware decoding is off for the replay: the GPU decoder refused a 1440p60 recording at load. The replay source also forgets its file at start-up instead of decoding last session's clip while OBS loads.
- **Icon crops are collected.** Every kill-feed row that involves you is saved at the size it was read into ClipHound's icons/harvest folder (up to 600), so the weapon icon templates can be recut at your own resolution rather than scaled from 1080p.
- **Music next to ClipHound.** The compilation also takes tracks from a music folder next to ClipHound.exe when the highlights folder has none; a set of Creative Commons tracks is delivered there with the attribution note they need.

## 0.13.1
- On one PC 0.13.0 froze OBS within a minute of starting, ending in Windows refusing OBS any more window objects. This build moves the only new timer to after start-up, stops the replay source from reopening its last file when a scene collection loads, and logs how many Windows objects OBS holds whenever the count climbs, so the next occurrence names its cause by time.

## 0.13.0
- **The highlights compilation, built on this PC.** ClipHound now ships with ffmpeg and cuts the compilation itself: as each clip lands, its action is pre-cut into a cached segment (5 s before the first kill to 3 s after the last, one at a time, at low priority, held back for 30 s whenever OBS reports dropped frames). When the stream stops, or when Play highlights finds nothing newer than your last clip, the best clips of the session (up to 12, by kill count, distance and tags) are joined with a title card and a kennel.gg card, a music track from a music folder inside the highlights folder is laid under it if there is one, and the file lands in the highlights folder. Play highlights then plays it full screen. The dock button reads Building... meanwhile and the status line shows progress.
- Settings, Clips: build at stream end (on by default) and the clip count.

## 0.12.3
- **The instant replay is silent by default.** The clip carries your mic and the game's sound from a minute ago, which is odd over the live stream. A tick box on the Clips tab turns its sound on, with the level next to it. The same applies to Play highlights.

## 0.12.2
- **The instant replay has a frame.** A light edge round the picture and an "Instant replay" tag in its top-left corner, with a blinking dot, so viewers know what they are seeing. The words are yours to change on the Clips tab. The highlights compilation plays without it.
- The Instant replay group on the Clips tab (start, end, size, sound, frame text, cooldown, chat, folder) is actually there now; 0.12.0 and 0.12.1 built without it, so those ran on the defaults.

## 0.12.1
- The POV name tag at middle-left now sits higher, above where the game draws the kill feed, so viewers can read the feed while a squad mate is on screen.

## 0.12.0
- **Instant replay.** The last highlight played back on the stream, cut to the action: 3 s before the first kill to 5 s after the last (both adjustable), at 75 % of the screen, centred, under your camera and alerts. On the dock, on a hotkey, and the plugin accepts a "!replay" from chat with a cooldown (60 s by default, 30 s to 15 min); reading the chat itself lands in the next build. Press the button again to stop.
- **Play highlights.** A dock button and hotkey that play the newest video in the highlights folder full screen, for the end-of-stream compilation. Settings, Clips has the folder and every replay setting, plus the Kick and YouTube channel names the chat trigger will read.

## 0.11.0
- **Clips know where the kill is.** ClipHound now tells the plugin when each kill-feed row appeared, and the plugin writes the position into the clip: the name ends in `@-7.4s` (the last kill, that many seconds before the end of the file) and a `.json` sidecar next to the file carries every kill's offset, the first kill of a multi-kill, killer, victim, distance and weapon. Kennel Cut reads both, so a compilation cuts on the moment instead of a guess. The sidecar follows the file through the rolling-highlights renames.
- Backtrack clips are now written to clips.csv like replay-buffer clips (they never were), with three new columns: moment, first kill, kill count.

## 0.10.10
- **The roster is fetched through Windows' own HTTP stack.** The Qt that OBS ships has no TLS backend on some PCs, and every read of the roster (and the update check) failed there with "TLS initialization failed", which 0.10.8 then took as "nobody is live with you". Both requests now go through WinHTTP. While the roster cannot be read, the dock says so and the squad shows as it would without a roster, with pop-outs deciding who is live.

## 0.10.9
- The Add pop-outs buttons on the dock and the Squad panel carry the reminder in their label: "(mute Discord stream before adding)".

## 0.10.8
- **Only who is live with you, properly.** The dock's drop-downs now hide everyone who is not streaming in your voice channel whenever the roster is on and you are a member, even before the first poll has answered; the dot next to names is gone from that list since everyone in it is live. When you are not in voice at all, nobody counts as live with you.
- The log now says what the roster reports ("Discord voice: 3 in voice, 1 sharing", or the error) each time that changes.

## 0.10.7
- **Swap over from further away.** Once a squad mate is on screen, a nearer one now takes over from up to 99 m away by default (it was 50, or whatever you had set); turn it down on the Switch tab if you want fewer swaps. The setting stops at 100 m, which is as far as the game's NEARBY list ever shows.

## 0.10.5
- **The dock's two drop-downs list only who is live with you.** With the Kennel.gg roster open, the squad-mate box and the Dual POV box offer just the people streaming in your voice channel right now; everyone else in the squad is managed from the Squad panel. Without the roster they behave as before.

## 0.10.4
- **Mute the stream in Discord.** Everywhere the plugin tells you to pop a stream out (Setup, the Squad panel, the Add pop-outs button and its result) it now also says to right-click the stream and mute it, since an unmuted stream's game sound plays in your headphones and goes out on your stream through Desktop Audio the whole time.

## 0.10.3
- **Discord's sound is left to Discord.** Discord hands OBS one mix for the whole call, so nothing outside Discord can pick one stream's sound out of it. The plugin no longer makes an audio capture for a Discord squad mate (the ones earlier builds made are removed once), nothing of yours is muted while a Discord feed is shown, and adding one says so. The POV sound button and its hotkey are gone with it.
- Twitch, Kick, YouTube and VDO.Ninja squad mates keep the sound that follows the picture: theirs plays while they are up and your game's sound is muted meanwhile, with the tick box on the Switch tab to turn theirs off.
- Coming back to your own POV only unmutes what the plugin itself muted, never an input you had muted by hand.

## 0.10.2
- **Their sound instead of yours.** While a squad mate is on screen, your own game's sound is muted and theirs plays: the game source when it carries audio (a capture card does), otherwise Desktop Audio, chosen once and listed under Settings, Switch, where it can be changed or cleared. Your microphone is never touched, and everything is put back exactly as it was when you are revived.

## 0.10.1
- **A Kennel.gg mark on the POV overlay.** While a squad mate is on screen, a small faint hound and "kennel.gg" sit bottom-right of the picture, out of the way of the name tag and the camcorder frame. On by default; the tick box on the Look tab turns it off. Not drawn in the Dual POV window.

## 0.10.0
- **Discord is the way squad feeds come in.** The network-discovery and network-share features, their settings group, the link test and the two optional installer downloads are gone, along with that squad-mate kind. Any slot of that kind in an old config is dropped on load. Everything else is untouched: Twitch, Kick, YouTube, VDO.Ninja and OBS-source squad mates still work.

## 0.9.3
- **Your Discord username fills itself in.** The Discord app on the PC is asked who it is logged in as (its local pipe answers that on a plain handshake; nothing more is asked of it) a few seconds after OBS starts, in Setup, and from the dock's Detect link. Typed names still work, and a mismatch between the two is said in the log.

## 0.9.2
- **Squad automation is for members of the Kennel.gg Discord.** The Kennel Ops bot now publishes its member list (hashed), and the plugin checks the Discord username from Setup against it. Not in the server, or no username entered: the roster is locked, the live buttons' place on the dock shows greyed out with **Join Kennel.gg Discord for more automation** and a link to enter the username, and the log says so once. Pop-outs and the manual controls keep working either way.
- **A Discord button on the dock** opens the Kennel.gg Discord through the plugin's own invite, so joins from the plugin are counted on their own line.
- The roster address and the channel name are no longer settings: it is always the Kennel.gg bot, and the channel is whichever voice channel you are sitting in.

## 0.9.1
- **POV sound, on the dock.** Whoever is on screen is the one feed with sound on your stream: their feed is unmuted, every other squad mate stays muted, and the unmute moves with the picture when the swap changes person. On by default now. The **POV sound** button next to Back to me turns it off and on again, the Switch tab's tick box is the same switch, and there is a hotkey for it under OBS Settings → Hotkeys. Your own inputs are never touched either way.
- Discord hands OBS one track for the whole call (every stream you watch, voices included), so for Discord squad mates that track is what turns on and off; a single stream's own level is still the slider on its pop-out.

## 0.9.0
- **A proper face.** The dock carries the Kennel.gg hound and wordmark in the brand's condensed face, the status line is a pill whose colour is the state (olive watching, red showing, grey off), the sections are labelled Squad, Dual POV, Clips and Events, every button and drop-down shares one height and edge, and the live buttons are olive chips that turn red for the one on screen. Colours are the brand's graphite, olive, amber and bone, and the styling is scoped to the plugin's own panels so OBS's theme is untouched. The settings window gets the same header and section titles.

## 0.8.25
- **Setup is built around Discord now.** The squad page of the setup explains the session flow (join voice, pop out squad mates' streams, press Add pop-outs), asks for your Discord username, turns on the voice roster so the plugin sees who is in your channel and who is live, and says plainly that the Kennel Ops bot needs to be in the server you play on for the best experience, with the link an admin uses to add it and the list of servers it can see right now.
- **Do not minimise a pop-out.** The setup and the log both say it: a minimised window stops drawing and its feed freezes. Tucked away is fine.
- The setup no longer reopens every start just because the squad is empty; with pop-outs that is normal. It runs until it has been finished once, or while there is no game source.

## 0.8.24
- **Add pop-outs and Show pop-outs are on the dock**, at the top. Add does the same as the Squad panel's button, in-game name prompt included, and the result shows on the dock's status line. Show pop-outs brings tucked pop-outs back to reach their controls and turns into Tuck pop-outs while they are out.

## 0.8.23
- **The voice roster outranks a leftover pop-out window.** Discord leaves a pop-out open after a stream ends, and a bound window counted as live even when the roster showed the person had left the channel or stopped. Now, whenever the roster knows your channel, it decides: not listed there means not streaming to you, whatever windows are still open. A bound window only counts as live when there is no roster to ask.
- Roster display names carry a rank in front ("Recruit Moriar"); a slot named Moriar now matches that, so people in your channel are not wrongly counted as absent.

## 0.8.22
- **A slot can no longer be quietly bound to the wrong person's stream.** The by-hand dialog preselects the first pop-out in its window list, and a slot typed as CYANIDE with gazreyn's pop-out left selected became "CYANIDE, showing gazreyn's stream", live whenever gazreyn was. The dialog now asks which you meant when the name and the picked window disagree, a slot named after someone else never takes that person's username, the Squad panel marks any slot still in that state, and the log says so at start.
- **Not in your channel means not live.** With the voice roster on and your username set, a Discord squad mate who is not in the channel you are in counts as not streaming, since they cannot be streaming to you.
- **Nobody near you streaming? Any live squad mate is shown.** When the NEARBY list has no one with a picture and the active squad mate is not live, the plugin switches to a squad mate who is, and says so in the log.

## 0.8.21
- **Any Discord server.** The voice roster now covers every server the Kennel Ops bot is in, and the Squad panel has a **Discord server** picker: any server the bot can see, or one in particular. Playing somewhere else? The panel shows the link an admin uses to add the bot to that server (it only asks to view channels), and that server's channels appear once it is in. The pop-out flow never needed the bot and works on any server; the bot is what tells the plugin who is live and who is not.
- **Live buttons at the top of the dock.** One button per squad mate who is streaming with a feed up. Press it and their feed takes the main view, press it again and you are back on your own POV. A button is there exactly as long as its person is live and its capture exists, so the row is empty when nobody is streaming. The one currently on screen is marked.

## 0.8.20
- **The dock's dropdowns list only squad mates who are streaming.** A slot the Kennel.gg voice roster has in voice but not streaming is left out of both the squad-mate and the Dual POV dropdown until they go live, and the ones known to be live carry a dot. Slots nobody can vouch for (Twitch, an OBS source, a Discord slot the roster does not know) stay in. If the one currently chosen stops streaming the dropdown shows nothing selected and reads "nobody streaming" when the list is empty. The lists refresh with every roster poll.

## 0.8.19
- **Only squad mates with a picture get shown.** A squad mate whose pop-out is bound, or who the Kennel.gg voice roster says is streaming, counts as live. One the roster has in voice but not streaming counts as off, and is never shown however close they are: Closest skips them for the nearest live one, and if the active squad mate is off when you go down, a live one takes their place, or you stay on your own POV with a line in the log saying so. Slots nobody can vouch for (Twitch, an OBS source, a Discord slot the roster does not know) are treated as live, as before. The Squad panel shows live / not streaming beside each slot.

## 0.8.18
- **Auto switch tick on the dock.** Untick it and the plugin never switches away from your own POV by itself, however many times you go down. Show friend's POV still works by hand, Dual POV still works, and clips keep coming. It replaces the Pause button, which did the same thing under a name nobody could read that from.

## 0.8.17
- **Weapon icons are read correctly at 1440p.** Every icon template was cut from 1080p footage, and the game draws its HUD a third bigger at 1440p, so on a 1440p feed the icons never quite fit any template and the small ones won by matching a slice of a bigger icon: a pistol read off the body of an assault rifle, a thin RPG tube read off a sniper's barrel, headshots missed. The kill feed is now scaled to 1080p size before matching, whatever the capture resolution. On the reference set of real icons this takes a 1440p feed from 16 wrong of 39 to 0 wrong.
- **Each icon is scored on its own, size included.** Icons are found as blobs and every template is scored against the blob with its size fit weighted in, so a small template cannot win on part of a big icon, and a skull beside a rifle reads as both. This also fixes two 1080p misreads (a Galil read as a car, a Kodiak pickup as a rifle), the small headshot skull no longer counting as a weapon, and most text streaks that used to read as a hunting rifle or an RPG. The thin-bar RPG template that caused the sniper mix-up is retired.

## 0.8.16
- **Number past clips into runs.** A button on the Clips tab, under Rolling highlights, goes through every clip already in the clip folders, finds the runs by the time in each file's name, and renames them [1 of 3], [2 of 3], [3 of 3] the same way new clips are. Lone clips are left plain, and a clip open in a player is skipped and said so. Twitch clips are not touched: their titles cannot be changed once they exist. The run window is a setting beside it.

## 0.8.15
- **Rolling highlights are numbered.** Clips made within 45 seconds of the previous one are a run, and their files are named "[1 of 3]", "[2 of 3]", "[3 of 3]", with the earlier files renamed as the run grows, so a chain of clips reads as one sequence in the folder. A lone clip keeps its plain name. On Twitch a clip's title cannot be changed once it exists, so the first clip of a run keeps its title and the later ones end in "part 2", "part 3". The 45 seconds is the clip series setting, sent to ClipHound as well.

## 0.8.14
- **Auto tick on the dock's Dual POV row.** Untick it before a match and the vehicle detector will not open the small window at all; tick it and the window opens and closes with the vehicle again. Force Dual POV works either way.
- **Only streams are ever captured.** A pop-out is treated as a squad mate's stream only when Discord titled it "<username>'s Stream". The whole call popped out ("General VC") and camera tiles are ignored everywhere: never added, never bound, never mentioned.
- **Discord audio level on stream** is a slider on the Squad panel. Discord mixes every squad mate's stream into one audio feed, so this is one level for all of them; for one person at a time, use the volume slider on their pop-out in Discord (Show pop-outs, or park them on another monitor).

## 0.8.13
- **Two or more tucked pop-outs no longer go black.** Every tucked pop-out went to the same spot at the screen edge, so the second sat exactly on the first and covered its sliver, and Discord stopped drawing the one underneath. They are now staggered down the edge so each keeps a stretch of its own showing. Parked pop-outs that do not all fit on the screen are staggered the same way instead of piled on one spot.

## 0.8.12
- **A pop-out that vanishes for a moment no longer costs the slot its capture.** A stream that hiccups, a window Discord redraws, a title blank for a second: the slot now waits 20 seconds before deciding the pop-out is really gone, and the capture is kept rather than deleted, so OBS re-hooks the window by itself the instant it is back. The 0.8.6 loop of "could not add ... to the scene" every two seconds came from deleting and re-creating that capture; it cannot happen now.
- **Settings saves retry.** A sync client holding the config file for a moment (Google Drive was seen doing it) made a save fail silently; it now tries five times and says clearly if it still cannot.

## 0.8.11
- **The name on the dual window is about four times bigger**, and the frame a little heavier, so it reads on stream. A **Name size** control on the Dual POV tab scales it from a quarter to four times, and the window updates as you change it.

## 0.8.10
- **Dual POV has its own row on the dock**: its own drop-down for exactly who goes in the small window, with Force Dual POV beside it. It has nothing to do with the squad-mate drop-down above, which is who the full-screen swap shows. Changing the Dual POV pick while the window is up swaps the person inside it.

## 0.8.9
- **Pop-outs can be parked on another monitor instead of tucked away.** On the Squad panel, "Pop-outs live" lists your monitors. Park them on the one the game and OBS are not on and every pop-out sits there fully visible, on top and stacked, so Discord keeps drawing them and each one's own volume control is a click away.
- **Show pop-outs** on the Squad panel brings tucked pop-outs back on screen to mute or adjust them, and tucks them away again when pressed a second time.
- **Every bound pop-out is sized to 16:9**, 1920 by 1080 or the largest that fits its screen, so Discord draws no letterbox bars and the capture is the whole picture at full size.

## 0.8.8
- **Force Dual POV shows the squad mate in the dock's drop-down.** Pick them, press the button, they are in the small window. The drop-down can move on afterwards for the full-screen swap and the window keeps its person. The hotkey does the same. The Squad panel's Show in Dual POV still puts whoever is selected there in the window, and swaps the person if the window is already up.

## 0.8.7
- **Add says exactly why when it cannot add someone, and the log keeps the whole line.** A source that was deleted moments earlier but still held somewhere kept its name and blocked a new one with that name; the plugin now moves the ghost aside and makes a fresh source. If a scene refuses the source, the message names the scene.

## 0.8.6
- **Pop-outs no longer go black when the game covers them.** Discord stops drawing a window that is completely hidden, and the capture went black with it. Every bound pop-out is now pinned above other windows and tucked to the right edge of its screen with a few pixels showing. Discord keeps drawing all of it, and the capture takes all of it. On by default; the tick is in the Squad panel, and turning it off puts the windows back.
  - Needs the game in borderless windowed mode. In exclusive fullscreen nothing can sit on top of the game, so a covered pop-out still goes black. The log says so when it tucks one.
  - Removing a squad mate puts their pop-out back where it was.

## 0.8.5
- **Both Add buttons do the same thing.** The Add on the Settings Squad tab now adds every popped-out Discord stream by itself first, exactly like the Squad panel, and only opens the by-hand dialog if there is nothing to add or you ask for it. Your log showed Add working three times and then a slot called "Discord" appearing from the by-hand dialog: that was its default name when the name box is left empty.
- **The by-hand dialog names a pop-out after its owner.** Pick "Pop-out: superfs1's Stream" and leave the name empty and the slot is called superfs1, with the username set so the watcher and the NEARBY matcher know who it is.
- Removing a squad mate from the Settings tab now logs it and updates the pop-out watch, the same as the panel.

## 0.8.4
- **The dual window showed nothing for a Discord squad mate.** Their capture sits in the main scene under the "warm" hide filter while you are alive, and the small window used the same capture, so it came up transparent there too. The filter now comes off while the window is up and the main-scene copy is hidden instead, which keeps the capture just as warm. Twitch and other web feeds were not affected.

## 0.8.3
- **The Discord window list shows pop-outs only.** When you add a squad mate by hand, the window picker lists only popped-out streams (Discord titles those with "Stream"), with "Any Discord window" at the bottom for the not-popped-out case. If nothing is popped out it says so instead of offering the wrong windows.
- **Add tells you what it saw.** When Add finds nothing to add, the panel and the log list every Discord window that was open, by title, so a pop-out that is named differently from what the plugin expects can be read straight off the panel.
- The owner of a pop-out is read with whichever apostrophe Discord uses, and for names ending in s.

## 0.8.2
- **Force Dual POV.** The dock button is now a forced toggle: press it and your squad mate is in the small window until you press it again, whatever the vehicle detector thinks. The detector still opens and closes the window by itself when the forced toggle is off, and the button says which of the two is holding it up.
- **The dual window has a frame and their name.** A thin edge round the picture and a small name plate bottom-left, drawn by the same look as the main swap so it matches, and scaled down because the window is. Off and on with the look effects, or by its own tick on the Dual POV tab.
- **A Discord username also finds them in the NEARBY list.** Both their in-game name and their Discord username go to ClipHound, whose matching is already fuzzy, so a slightly different spelling in the game still matches.

## 0.8.1
- **Add asks for the in-game name.** When a popped-out stream becomes a squad mate, the panel asks what they are called in the game, with the Discord username filled in as the guess. That name is what the NEARBY list is matched on, so Closest works from the first match. An **In-game name...** button on the panel changes it later.
- **Dual POV turned on by hand stays on.** Leaving a vehicle only closes a window the vehicle detector opened. Pressing Dual POV with nobody picked uses the active squad mate. The Squad panel has **Show in Dual POV** for whoever is selected.

## 0.8.0
- **Squad button on the dock, and the flow is now: pop it out, press Add.** Open OBS, join Discord voice, watch a squad mate's stream and pop it out. Press **Squad** on the dock, then **Add popped-out Discord streams**: every popped-out stream becomes a squad mate named by their Discord username, bound to that window by exact title, with their in-game name set to match. Already-added people are skipped. The panel also lists the squad with what each one is showing, and has Make active and Remove for mid-broadcast.
- **Window title must match.** A slot made from a pop-out, or from a specific Discord window you picked, is matched on that exact title only. Matching by executable was how a slot ended up on the wrong Discord window; it is now used only for "Any Discord window".
- **Squad mates who go live in Kennel.gg voice can add themselves.** One tick in the Squad panel. The roster address is built in, nothing to paste. Slots are named by Discord username so they line up with pop-outs and in-game names. Put your own Discord username in the panel and only the channel you are sitting in counts, and your own stream is never added.

## 0.7.9
- **Pop-outs are matched on their owner exactly.** A real PC showed a Go Live pop-out is titled "<username>'s Stream". The plugin now reads the username out of that and binds a slot whose Discord username, or slot name, is exactly that person, so a slot called Bryan can never take bryanx's window. A hand-named slot still matches if its name is inside the username.
- When a pop-out belongs to nobody in the squad the log names the Discord user it belongs to, and says when that user is you.

## 0.7.8
- **Your desktop audio was being ticked to mute again on every start.** The "already done that" flag behind the old auto-pick was never saved to disk, so whenever the mute list was empty at start-up the plugin ticked your desktop audio back in, undoing you if you had cleared it. The auto-pick is gone for good: nothing of yours is muted unless you tick it in Settings -> Switch. The list is cleared once more on first start, since anything in it may have been the auto-pick's doing.

## 0.7.7
- **Pop-out binding now covers every Discord squad mate**, including slots where you picked a specific Discord window when you added them. 0.7.6 only watched slots set to "Any Discord window", which is not what the add dialog picks by default when Discord is running, so for most people it watched nothing. The log now says at start how many slots it is watching.
- A slot bound to a pop-out remembers the capture it came from and goes back to exactly that when the pop-out closes.

## 0.7.6
- **Pop-outs bind themselves.** Pop a squad mate's share out of Discord (right-click their tile, Pop Out) and within two seconds their slot is showing that window and nothing else. Close it and the slot goes back to the Discord window. Two or three pop-outs give you two or three separate feeds. The log says whose window it found and what it was called.
  - Discord titles a popped-out tile with the person's username, so the roster now carries usernames too. If a pop-out shows up that matches nobody, the log names it so you can see why.
  - A pop-out that is minimised freezes; the log tells you to restore it. It can sit behind the game, just not minimised.
- **One capture of the Discord call, not one per squad mate.** 0.7.5 made every squad mate their own capture and audio capture of the same Discord window. Five squad mates meant five captures of one window, all showing the same picture. They share one now. Existing slots are moved over on first start.

## 0.7.5
- **Squad slots fill themselves in from Discord.** Somebody goes live in your voice channel and a slot appears with their Discord name on it, their capture already made. They stop sharing and the slot goes away again. Slots you added yourself are never touched.
  - Turn it on in Settings -> Squad, under **Squad from Discord**, and paste the roster address from the server. It carries a key, so treat it like a password and keep it off stream.
  - Discord will not tell a plugin who is in a call - the client's own interface for that is gated behind a permission Discord grants application by application, by hand. The Kennel.gg Discord bot publishes the roster instead, which is why this only works for our server.
  - Only people actually sharing get a slot. Discord puts every share inside the one window, so a slot for somebody who is not live could never show anything.
  - Popping a share out is still a click in Discord. Nothing here moves your mouse for you.

## 0.7.4
- **ClipHound stuck on "starting" with no clips and no NEARBY, explained and fixed.** From a user's OBS log: the bridge was on `47820` and everything worked, then two hours later `bridge: listening on ws://127.0.0.1:47821` and ClipHound never connected again. Nothing was pressed - a mouse wheel over the settings window had rolled the **Bridge port** spin box by one. ClipHound reads that port from its own config.yaml, and the plugin never told it, so it went on knocking at the old number for ever.
  - **A scroll no longer changes a setting.** Spin boxes, sliders and drop-downs ignore the wheel until you click into them; scrolling moves the page, as it should.
  - **ClipHound is told the new port** and restarted, so the two can never disagree. If its config cannot be written the log says so and what to do.
  - If you are on an older build and see this: set **Bridge port** back to **47820** (Settings -> ClipHound).
- **Twitch clips stopped being made after a while, and now they will not.** Twitch hands back a *new* refresh token every time the old one is used, and invalidates the old one - ClipHound updated it in memory and printed "update config.yaml yourself", so the next start sent a dead token and got `400 Bad Request` for ever after. The new pair is written to config.yaml the moment it is refreshed, and a 400 now says in plain words that the login has expired and where to fix it, in the plugin's own log rather than only ClipHound's.
- **ClipHound sitting at "starting" said nothing about why.** A failed connection to the plugin was swallowed - the app retried silently every 3 seconds for ever. It now says what the connection error was and what to check, once, and the plugin prints the tail of ClipHound's log if 25 seconds pass with no connection.
- **Setting the clip length could switch the replay buffer off.** 0.7.2 stopped the buffer and started it again on the next line, but OBS's stop has not finished when the call returns, so the start silently failed - leaving the buffer off and no clips at all until OBS was restarted. The restart now waits for the stop, checks it came back, and says so if it did not.
- **ClipHound stuck on "starting", and no clips: now it tells you why.** If two copies of the plugin are installed, both load and the second one cannot open ClipHound's bridge port - ClipHound then connects to the wrong one and sits at "starting" for ever, with no clips. That failure only ever reached OBS's own log, so from the dock it looked like nothing at all. It now says so in the plugin's log, names the old folder to delete, and the installer removes `plugins\kennel-wardogs` itself - or says plainly that it could not, which almost always means OBS was still open.
- **And if ClipHound starts but never connects**, after 25 seconds the plugin prints the last lines of ClipHound's own log into its own, instead of leaving "starting" on screen with no explanation.

- **Replay files are named the same as the Twitch clip.** ClipHound now hands the plugin the same plain-English headline it gives Twitch, and the default file name is that headline followed by the date and time - `Double kill at 68m and 61m with a rifle - 2026-09-11 21-04-33.mkv`. The tag list has gone from the default name (it is still in the clip index, and `{tags}` still works in a template of your own). A template you typed yourself is left alone.

## 0.7.2
- **Twitch clips are titled with what happened.** They used to carry the stream's title; the moment that set them off was only written to ClipHound's own index. The clip now goes to Twitch with a plain-English title - *Double kill at 68m and 61m with a rifle*, *Died to a headshot at 120m*, *Crashed my chopper* - and the same wording names the OBS replay file.
- **Clip length is a setting: 45 seconds by default.** Clips tab -> *Clip length*. It is written straight into OBS's own replay-buffer setting for both output modes, so the plugin and OBS never disagree about how far back a clip reaches, and the buffer is restarted if it was running.
- One honest limit: the 45 seconds is for the clips OBS saves. A Twitch clip's length is Twitch's - the API takes the seconds leading up to the request and its edit page trims afterwards; nothing the plugin sends can make one 45 s long.

## 0.7.1
- **The 0.7.0 installer put the plugin in the wrong folder, so OBS never loaded it** - no dock, no Tools entry, no error. It kept the old AppId so Windows would see an upgrade, and Inno Setup then reused the previous install folder: `kennelgg.dll` ended up under `plugins\kennel-wardogs`, and OBS only loads a DLL named after its folder. 0.7.1 always installs to `plugins\kennelgg` and removes the stray folder 0.7.0 left. Nothing else changed.

## 0.7.0
- **Renamed to Kennel.gg Wardogs Streaming Tool, all the way down.** The window, the dock, the hotkey labels, the installer, the repo - and now the plugin itself: the module is `kennelgg`, it installs to `plugins\kennelgg`, ClipHound lives in `ProgramData\Kennel.gg\ClipHound`, and every source the plugin makes is named "Kennel.gg ..." ("Kennel.gg · Pup", "Kennel.gg web", "Kennel.gg look", "Kennel.gg dual"). The Help tab has an **About The Kennel** section with what kennel.gg is and links to the site, Discord, Twitch and X, and the installer's welcome page says who made it.
- **Nothing is lost in the move.** The installer removes the old `plugins\kennel-wardogs` folder (two copies would both load), carries ClipHound's config over and removes its old folder; the plugin picks up its old settings file the first time it starts under the new id; sources made by earlier builds are renamed in place rather than made again, so scenes do not fill with duplicates; and the hotkey ids are unchanged, so bindings survive. Close OBS, run the installer, start OBS: that is all.
- **Nothing of yours is muted when the POV changes, and no sound is taken from the squad mate's feed** - for everyone, including setups that had either ticked. Both are still there on the Switch tab to turn on. A Discord squad mate's audio capture follows the same rule now in every mode.
- **Kick and YouTube live streams** as squad-mate kinds, alongside Twitch. Kick takes the channel name; YouTube takes a channel link, @handle, channel ID or a live video link - a handle is looked up once on Save for the channel ID the player needs. With a channel, whatever they are streaming right now is shown.
- **The swap on a vertical canvas too.** Switch tab -> *Vertical scene*: pick the scene your portrait stream (Aitum Vertical) shows, and the squad mate's feed is shown there as well - full height, sides cropped - with the look overlay in a portrait form: everything sized to the narrow canvas and the POV tag across the top, where the cropped feed has no HUD. Same source in both scenes, so nothing is decoded twice; the *at* box on the Look tab still moves the tag.

## 0.6.6
- If the picture still judders, the size being sent is the next thing: **Share at** on the *sending* PC. A full 1440p canvas is about 240 Mbit of nearly-raw video; 1080p is about a third of that and 720p a tenth.

## 0.6.5
- **Deleting a squad mate offers to delete the sources made for them.** *Remove and delete the sources* / *Remove, keep the sources* / *Cancel*, with the sources listed so you can see what will go. Only ever ones the plugin made: a squad mate set up as an OBS source you already had keeps it, and the shared browser source is never touched.

## 0.6.1
- **It restarts a share that has died**, every 15 seconds, and says so in the log.

## 0.6.0
- **Every settings tab scrolls.** At 125 % Windows scaling, on a laptop screen or with a large font the contents were squeezed into whatever height was left instead of keeping their own. The window can also be made genuinely small now.

## 0.5.9
- The forced "normal latency" write is gone with it. The only receive setting the plugin sets by itself is the one you choose in **Receive at**.

## 0.5.7
- **Share at is sizes only** - 720p, 900p, 1080p or the full canvas, always at your OBS frame rate. That also removes the frame-rate halving that made a smaller share look choppy: 720p is now 720p at 60 if that is what you run.

## 0.5.6
- **Share at now covers both halves of the trade-off.** 0.5.4 only offered 30 fps, so choosing a size that a network could carry also halved the frame rate - steady, but soft and visibly half the frames. Every size now has a 30 and a 60: 720p, 900p, 1080p and the full canvas. The default moves to **1080p 30**, about three times the picture of 720p 30 for a third of a gigabit link; pick 720p 60 instead if motion matters more to you than sharpness.
- The downscale uses **Lanczos** rather than bicubic - noticeably sharper at 720p and 1080p, and it costs the network nothing.
- **Timing**, per squad mate in Edit...: frame sync (the default), network timestamps, the sender's timecode, or none. If a feed still judders, these are worth trying in turn; senders differ.
- **Test feed** button on the Switch tab. It watches the selected squad mate's feed for two seconds and tells you how many new pictures a second are actually arriving - which is the only way to tell a feed that is not being delivered from one that is arriving fine and being drawn badly.

## 0.5.3
- **A squad mate's Discord share shows their game, not their Discord window.** Their screen share arrives inside Discord's own window - flat grey down the sides, black letterboxing around the picture. The plugin now renders a frame of their feed, walks in from each edge while the whole row or column is still one flat colour, and crops the scene item to what is left. It runs each time their feed goes up, so it follows the window being resized, and never takes more than a third off any side. Turn it off per squad mate with **Borders** in Edit....
- **The POV tag has moved off the map.** It sat bottom-left, over the game's minimap and the score along the bottom. It now sits halfway up the left-hand side, clear of both, and there is an **at** box next to the name tag on the Look tab to put it middle, top left, top centre, bottom left or bottom right instead. Existing setups are moved once.

## 0.5.2
- **Your camera and your alerts stay on top.** A new **Always on top** box on the Switch tab: tick your face cam and your alert overlays and they are lifted back over the top every time the plugin shows a squad mate, brings up the Dual POV window, adds a source or puts the look overlay on - nothing of ours can cover them. The list is the stacking order, first is the topmost, and you can drag it around. Your camera and anything that looks like alerts are ticked for you the first time you open it.
- **A squad mate's Discord feed no longer flickers.** Saving settings re-applied the window-capture settings to a source that already had them, and Windows tears the capture down and starts it again each time. The plugin now writes settings only when something has actually changed. Probing for the window list is also cached for a few seconds - it briefly makes a second capture of the same window, which is the other half of the flicker.
- **Your own placement is left alone.** A squad mate's source was stretched back to the full canvas on every save, so if you had moved or resized it, it snapped back under you. It is only placed when it is first added; after that it is yours.

## 0.5.0
- **A bright sky no longer reads as alive.** The damage-log panel is see-through, so what is behind it changes how the header looks: aim at the sky, drive through smoke or take a muzzle flash and the wording washes out for a moment. The score dipped, and with a single poll enough to end the swap, your own POV came back while you were still on the floor. The match now runs on the picture with its local brightness taken out - the sky's brightness and its gradient go, the letter strokes stay - so the score barely moves when the background changes. Measured on the frame that was failing: a washed-out header that scored 0.75 before (under the threshold, so "alive") now scores 0.94, and a heavily blown-out one 0.83 where it used to score 0.57.
- **And it holds through a washout anyway.** Once you are down the log counts as still there while it scores above the hold level (the threshold less 0.15 by default, on the Detect tab) *and* stays in the place it was found - so nothing elsewhere on screen can pin you down either. The hold is a bridge, not a latch: if the log has not scored a clean match for three seconds it lapses. Coming up now also takes two polls rather than one (existing settings are moved).
- The Detect tab has a **Hold down to** slider showing the score the log is held at.

## 0.4.9
- **Detection works across resolutions.** The gap between the **B** key hint and "VIEW DAMAGE LOG" is a different fraction of the screen at 720p, 1080p, 1440p and 4K, so the old template - which spanned the hint, the gap and the wording - could only ever be a near miss on a resolution other than the one it was cut from. The built-in template is now the wording alone, which is the same shape everywhere and only changes size, and the game source is read at 1000 px across instead of 800 so that small text has enough pixels to match on. On the 1080p frame that was failing this scores 0.86 where everything else on screen scores 0.51; before it was 0.82 against 0.63.
- The match threshold default moves from 0.85 to **0.80** to suit the new template, and existing settings are moved with it (including the ones dragged down below 0.75 to try to make the old template work).

## 0.4.8
- **Learn my HUD** (Detect tab), for a damage log that is never quite matched. Press it while downed: the plugin shows you what it found, and on your say-so cuts that header out of your own screen and uses it from then on, so the score goes near 1 instead of sitting just under the threshold. The built-in template was cut from one particular screen, and the gap between the **B** key hint and the wording is not the same on every HUD - which is exactly the near miss that made one tester drop the threshold to 0.6 and then be shown as downed permanently.

## 0.4.7
- **When the damage log is never found.** The Detect tab has **Save a frame...**, which writes a PNG of your game source exactly as the plugin sees it; do that while downed and send it, and the HUD it cannot match can be looked at directly. The usual search area is also wider than it was (from 45 % across and 15 % down, was 60 % and 25 %), and **Look over the whole frame, at more sizes** tries everything from a third to twice the expected size for a HUD the normal search misses.
- Dragging the match threshold below 0.75 now says, in red, that it will match almost anything - which is what a "downed all the time" reading means, not a fix for a log that is never found.

## 0.4.6
- **The game source can be chosen on the Detect tab too**, right above the picture, instead of only on the Switch tab. It is the same setting in both places.
- **The Help tab shows which version you are running**, and whether a newer one is out.
- **You are told when there is a newer build.** On start-up the plugin asks kennel.gg for a small file saying what the latest build is; if yours is older, the dock shows a line with a download link and the Help tab says the same. Nothing about you is sent, there is no account, and it never installs anything by itself. Turn the check off on the Help tab.

## 0.4.5
- Dual POV tab: **Leave the window up when I get out of the vehicle**, off by default. Off, the window goes by itself when you get out; on, it stays until you turn it off.

## 0.4.4
- **Dual POV goes when you get out of the vehicle, however it was turned on.** While the window is up, ClipHound watches the vehicle keybind corner and three reads with nothing there hide it - from the dock button and the hotkey too, not only when it came on by itself. Turning it on by itself still needs the tick box.

## 0.4.3
- **Crash when closing OBS with Dual POV on, fixed.** The window's private scene could not be found again by name, so every time it was applied a new scene and a new browser page were created and never released; with the window set to come on at start-up that happened on every launch, and OBS then crashed inside the browser engine on the way out. One scene is now kept and released properly before OBS unloads its modules, and the window comes on two and a half seconds after loading, once the browser module is up.
- Detect tab wording: the damage log stays up the whole time you are downed except while the Escape menu is open; with the menu open the plugin comes back to your POV until you close it.

## 0.4.2
- **Dual POV turns itself on in a vehicle** (Dual POV tab, "Turn the window on by itself..."). ClipHound reads the keybind list the game draws bottom-right while you are in a vehicle - CYCLE WEAPON is the tank gunner, DEPLOY SMOKE the tank driver, COLLECTIVE LIFT / DEPLOY FLARES the Havoc pilot, INTERACT and ZOOM alone the Havoc gunner's CAM view - and the window comes up with that seat's placement, then goes when the list goes. A seat is acted on after two readings in a row and "out" after three, so a covered corner does not flap it. One small OCR run a second, only while the option is on. The blue box on the Dual POV picture is where that list is; drag it if your HUD differs.

## 0.4.0
- **Dual POV** (new tab). Two of you in a tank or a Havoc: your own POV stays on screen and your crew mate's feed sits in a small window over it, placed where the game draws nothing. Pick the crew mate, pick the vehicle and the seat you are in - tank driver, tank gunner, Havoc pilot, Havoc gunner in the CAM view - and the window goes where that seat's HUD leaves room (top-left between the team chat and the kill feed; inside the picture frame for the CAM view). Or Custom: drag the box on the live picture, or type left, top and width. Opacity slider. Picture only, no sound. A **Dual POV** button in the dock and a hotkey ("dual POV window on / off") turn it on and off; it comes back on with OBS if it was on. When you go down the window steps aside for the full-screen swap and returns after.

## 0.3.9
- **Closest cannot be turned on without ClipHound running.** The dialog offers to start ClipHound; the tick box stays off until it is connected.
- **Settings window no longer opens squashed.** It came up with every control squeezed to a few pixels until you resized it by hand: the first paint used the geometry from before the window was sized. It now opens at a proper size and lays itself out again the instant it appears. Same for the setup wizard and the Logs window. Fonts sized in pixels by the OBS theme are handled too.

## 0.3.8
- **Only squad mates with a feed count, and the nearest of those wins.** Players near you who are not set up as a feed are never matched, so with five in the squad and two streaming the POV goes to whichever streamer is nearest, however far. The "... m or closer" range rule now only holds the feed on a squad mate who is still in the NEARBY list; if they have left it, any streaming squad mate in the list takes over. If no streaming squad mate is in the list at all, the squad mate already selected stays.

## 0.3.7
- **Closest says so when ClipHound is not running.** Ticking Closest in the dock or in Settings without ClipHound running asks whether to start it (it reads the NEARBY list; nothing works without it). The dock's Nearby line turns red and says the same while it is off, and the Settings tick box is labelled "needs ClipHound running".

## 0.3.6
- **Back up means your own POV, full stop.** On the way back every squad mate's video and audio is hidden in every scene, whatever state it was in. Warm and preloaded feeds now sit invisible rather than transparent (browser sources keep running while hidden), so nothing can be left showing.
- The dock's Nearby line reads **N/A while you are up**.
- The dock's **Closest** tick box and the Settings one are the same switch and stay in step; ticking it in the dock does everything ticking it in Settings does.
- **Swap over only for someone ... m or closer** (Switch tab): once a squad mate is on screen, the feed only moves to a nearer one who is within this distance, 50 m by default; 0 means any distance. The pick when you go down is not limited.

## 0.3.5
- **The closest squad mate is actually the one shown.** While you are going down, every NEARBY reading now picks the nearest squad mate outright. Before, a "15 m closer" rule meant to stop flapping was also blocking the first pick: someone at 4 m was not "15 m closer" than the one at 12 m, so the feed stayed on whoever was already selected. That rule is gone; once a squad mate is on screen, only the **Wait between swaps** slider holds a swap back, and the log says when it does and for how long.
- **The NEARBY list is cleared the moment you are back up.** Who was near you while you were down is not relevant once you are alive, so the dock shows nothing until the next time.

## 0.3.4
- **A distance that cannot be read no longer throws the squad mate away.** The last distance actually read for them is used for up to 12 s, so one bad frame in a burst does not turn them into a question mark and does not change who is closest.
- **Fewer unreadable distances.** The little chip is now accepted when it reads as a bare number (the reader only ever gets digits and an m there), a bright blob that is not a chip is rejected instead of being read as one, and the whole row is read as a last resort when the chip gives nothing.
- **Squad-mate feeds can be preloaded** (Switch tab → Extras, off by default): every squad mate's feed sits in the scene loaded, playing, invisible and silent, so a Twitch feed is not starting up when the swap happens. Each preloaded feed uses its own bandwidth, which is why it is off unless you ask for it.
- **Your own game sound is no longer muted by default.** The squad mate's feed comes in silent instead, so you keep hearing your own game while your stream shows their POV. "Play the squad mate's game sound" turns theirs on, and the list next to it is where you tick anything of yours to mute. Existing setups had your desktop audio ticked automatically by an older version; that is cleared once on upgrade.
- The log names the squad mate, their in-game name and the whole reading it chose from, so a wrong pick is obvious.

## 0.3.3
- **Wait between swaps** slider (Switch tab, "Show whoever is closest"): how long the feed stays on one squad mate before it may swap to a closer one while you are down. 1 to 10 seconds, 4 by default. Low values follow whoever is nearest as they run to you, high values pick one and leave it. The swap the moment you go down never waits, whatever this is set to.

## 0.3.2
- **Test read** button on the Detect tab, next to the NEARBY box: it shows the box as the plugin sees it and exactly what ClipHound reads there - the rows it found, the names, the metres, and whether any of it matched a squad mate. It also says what to change when nothing matched. ClipHound saves the picture it looked at as `nearby_debug.png` in its own folder.
- **Several reasons the NEARBY list read nobody, fixed.** When the distance chip was not found the row was cut short before the metres, so the fallback could never find them; the whole row is read now. The chip is found as the rightmost solid box with much looser limits. A second way of picking out the text is tried when the scene behind the panel is bright, where the first one turns the whole crop into one blob. Names are matched more loosely, on letters and digits alone. A squad mate whose name is read but whose distance is not still counts as nearby.
- **The NEARBY list is only read while you are down.** Nothing is read between fights: the plugin asks the moment the damage log appears and keeps asking until you are back up, so it costs nothing while you play.

## 0.3.1
- **The kill feed keeps up now.** Each row used to cost four separate tesseract runs per frame, so with three rows on screen ClipHound managed under two frames a second and a kill took four or five seconds to come out. All the rows in a frame are now read in one run, and a row is decided 0.75 s after it is first read whatever the frame rate. A kill reaches the dock about a second after it happens. (The clip file still lands about four seconds later on purpose, so the moment is inside it: Clips tab.)
- **Closest actually drives the feed.** The squad-mate box in the dock now follows the closest one by itself while the new **Closest** tick box next to it is on, and only that box is used when it is off. Nothing changes mid-swap unless someone is clearly closer (15 m) and not more than once every four seconds; the moment you go down that guard is dropped so you always get the nearest one.
- **The NEARBY reading stops flickering.** A single bad frame no longer wipes the list: ClipHound only reports an empty list after three misses in a row, the plugin keeps the last good reading, and the dock says what it is waiting for instead of "nothing read yet".
- The blue **NEARBY box moved to the Detect tab**, next to the damage-log picture, since it drives the POV switch and not the clipping. The kill-feed box stays on the ClipHound tab.
- The log says why a nearby squad mate was not switched to: not in your list, or their feed source is missing.

## 0.3.0
- **Show whoever is closest.** The game's NEARBY list (bottom right of the HUD) is read while you play, so when you go down the POV that comes up is the squad mate who can actually reach you. Switch tab: turn it on, choose whether to keep following the nearest one while you are down, and how much closer someone must be (default 15 m) before the feed swaps over mid-swap. Each squad mate needs the name the game shows for them: Switch → Edit... → In-game name. Needs ClipHound running; without a reading the squad mate you picked is used exactly as before.
- **The kill-feed and NEARBY areas are picked by dragging on the live picture** (ClipHound tab): choose which of the two the drag sets, drag a box, done. It goes to ClipHound straight away, no restart. This is the picker that 0.2.24 announced but did not actually ship.
- **The kill feed is read twice as fast.** The default rate is now 10 times a second (was 5) and is on the ClipHound tab. A kill is decided after about 0.75 s of reading whatever the rate, so a kill now becomes a clip in about a second instead of two.
- **A kill you cover up still clips.** A feed row that disappears early - you opened the inventory or the map, or a multi-kill pushed it off - is decided on what was read, and a row with your own name in it is decided on two reads.
- Dock shows the NEARBY reading while the feature is on.

## 0.2.24
- Kill-feed area sent to ClipHound live (the picker for it arrived in 0.3.0).

## 0.2.23
- Switch delays, on the Detect tab: the squad mate is shown 2 s after you go down by default (a revive inside that window never switches), and you come back instantly on revive (0 ms). Both editable.

## 0.2.22
- Clip file names put what happened first, then the date and time: `{title}_{tags}_{date}_{time}`. Existing configs on the old default are migrated; the template stays editable on the Clips tab.

## 0.2.21
- Settings, setup wizard and Logs windows always open fully on screen (their title bar could sit above the screen edge and be impossible to grab).

## 0.2.20
- Backtrack file naming: the output folder is found from any Backtrack/Aitum source or filter setting that looks like a path, subfolders are scanned, the watch lasts 90 s, and the log says which folders are watched (or that none is known - set it under Settings → Clips).

## 0.2.19
- Dock: the damage-log match bar is replaced by "Downed state detector: Alive / Downed".

## 0.2.18
- Switching back is as fast as switching away. The cause was the "REVIVING" search on the friend's feed running in full on every poll while the friend was on screen, stretching each poll; it now runs in full every 5th poll with a cheap check in between. Confirmation is 2 polls each way and the minimum time on the friend is 0.5 s.

## 0.2.17
- Switch-back also reacts to the score falling away from its steady level; 2 polls, 0.5 s floor. Existing configs migrated.

## 0.2.16
- Discord always saves: "Any Discord window" matches by executable and follows the pop-out when it appears; errors show in red.
- VDO.Ninja quality per squad mate: resolution (720/1080/1440), frame rate, bitrate ceiling, codec. Default 1080p60, 12000 kbps, H.264. The friend's link and the plugin's viewer link follow the settings.

## 0.2.15
- POV swap reacts in ~0.2-0.3 s instead of ~1 s: the plugin remembers where the damage log was last found and checks that spot on every poll (10 per second) at almost no CPU cost; the full search still runs only every 600 ms while you are alive. Confirmation is now 2 polls down / 4 polls up. Existing configs are migrated.
- `CHANGELOG.md` added.

## 0.2.14
- CI builds Windows only (Actions minutes).

## 0.2.13
- Aitum Backtrack is detected. If OBS's replay buffer is disabled and Backtrack hotkeys are set, clips use Backtrack only and the dock says "Backtrack ready" with its folder instead of nagging about the replay buffer.

## 0.2.12
- Kill feed: a row pushed down a slot by a new kill is followed instead of being counted as a new row, and a row that vanishes is still decided with the reads it had. Every kill in a multi-kill now shows and counts. 5 frames per second, 7 reads to decide.

## 0.2.10
- ClipHound heals a corrupt `config.yaml` (sets it aside, starts from defaults, the plugin pushes your settings back) and writes its config atomically.
- Dock: Start / Stop ClipHound button with real state (connected, starting, crashed) and a Save clip split button with tagged saves.

## 0.2.9
- "Clip every kill I get" option; multi-kill window 30 s by default and editable in OBS.
- Same kill decided twice (with and without the weapon icon) now counts once.
- Backtrack clips are renamed with the same title and tags as replay-buffer clips.
- Clear warning when OBS's replay buffer cannot start; one ClipHound instance at a time.

## 0.2.8
- ClipHound runs windowless; no console setup anywhere. All of its settings live in OBS.

## 0.2.7
- Twitch login from inside OBS (device code flow with the Kennel app id).
- Dock shows an event feed (kills, triggers, downed / back up). Backtrack "Save" hotkeys listed first. Logs tab in Settings. One settings window at a time.

## 0.2.6
- ClipHound tab in Settings: in-game name, clip folder, library index, Twitch on/off, channel to clip.
- ClipHound closes when OBS closes.

## 0.2.4
- Readable helper text in dark themes; Settings and wizard are resizable; Logs button on the dock; ClipHound writes `cliphound.log`; look overlay always hidden on the way back; ClipHound launch fixed; Aitum Backtrack / any-hotkey clip triggers.

## 0.2.1
- Fixed a crash when opening Settings from the dock.

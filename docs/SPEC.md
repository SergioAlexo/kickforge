# Kickforge feature-parity spec (target: Sonic Academy KICK 3 functionality)

Functionality only. Own name, UI, presets and sounds. No third-party samples, artwork or preset data.

Research basis: public reviews and press material. The official manual and product page were not
retrievable (sonicacademy.com returned 403, MusicRadar body truncated). Details such as exact ranges
are therefore mostly unconfirmed and marked "(unverified)". Items without that marker appear in at
least one cited source. Ranges given as "suggested" are our design choices, not KICK 3 facts.

## Sources
- https://musictech.com/reviews/plug-ins/sonic-academy-kick-3-review/ (most detailed: layers, FX, EQ, limiter, trigger, import)
- https://rekkerd.org/sonic-academy-releases-kick-3-kick-drum-synthesizer-plugin (release feature list)
- https://www.kvraudio.com/news-print.php?id=62006 (KVR news, same feature list)
- https://www.kvraudio.com/forum/viewtopic.php?p=8969350 (KVR thread with developer replies: selective key tracking, click start/delay, no randomizer)
- https://musictech.com/guides/buyers-guide/best-kick-drum-plugins-2026-tested-reviewed/ (summary: 5 layers, pitch/amp curves)
- https://sonicstate.com/news/2026/01/29/lift-kicks-from-any-track/ (import/analysis, not read in full)
- https://www.musicradar.com/reviews/tech/sonic-academy-kick-595389 (review of KICK; body not retrieved)

## New in KICK 3 vs KICK 2 (per reviews)
Up to 9 extra sub harmonics; 15-effect FX section with buses; linear/log curve views; phase lock; selective
key tracking; AI import/analysis of kick samples (also isolate from loops); Trigger (sidechain drum
replacement) tab; output limiter; EQ tab with analyser; 4 macros; WAV export; resizable UI; Bezier
curves; undo/redo; zoom/pan; pitch snap; gate. KICK 2 presets are loadable in KICK 3.

## S - Sub / body synth
- [ ] S-01 Up to 5 layers total, each layer is either a synth sub or a sample (click); shared layer slots with C-xx
- [x] S-02 Sub layer fundamental oscillator, sine-based
- [x] S-03 Additional oscillator waveform shapes beyond sine (unverified; plan: sine, triangle, saw-ish, square-ish, soft-clipped sine)
- [ ] S-04 Up to 9 additional harmonics per sub layer, each individually tuned (harmonic ratio / semitone offset)
- [ ] S-05 Per-harmonic Gain control
- [ ] S-06 Per-harmonic Decay control
- [ ] S-07 Harmonics-section amount envelope curve (added in a KICK 3 beta)
- [x] S-08 Pitch envelope per layer, node-based curve editor (see U-xx)
- [x] S-09 Amp (volume) envelope per layer, node-based curve editor
- [x] S-10 Pitch curve Bezier segments (curvature per segment)
- [x] S-11 Amp curve Bezier segments
- [ ] S-12 Pitch snap: node pitch values snap to musical notes (toggle)
- [x] S-13 Base tuning of sub in Hz and/or note name with fine tune in cents (unverified ranges; suggested 20-400 Hz, +/-100 cents)
- [x] S-14 Key tracking on/off per layer
- [ ] S-15 Selective key tracking: choose which pitch-curve node(s) follow MIDI note, so the tail transposes and the initial thump stays fixed
- [x] S-16 Per-layer phase offset, 180 degree flip at minimum (full 0-360 rotation, unverified)
- [ ] S-17 Phase lock: right-click a node locks the phase of the remaining tail so the early part can be edited without realigning the tail
- [x] S-18 Layer length / overall decay scaling that stretches the envelopes (length control; unverified exact behavior)
- [ ] S-19 Drive amount per sub layer with its own envelope curve (added in a KICK 3 beta)
- [x] S-20 Gate on the voice (end-of-sound gate / note-off behavior; unverified specifics)
- [x] S-21 Layer mute/solo and per-layer volume (unverified)
- [ ] S-22 Layer delay/start offset for the sub (developer did not commit to this; optional)

## C - Click / sample layers
- [ ] C-01 Layers can hold a sample instead of a sub (WAV at minimum; AIFF unverified)
- [ ] C-02 Sample start point adjustable
- [ ] C-03 Click delay (layer start delay), added after the initial KICK 3 build
- [ ] C-04 Sample pitch (semitone/cents) with optional key tracking (unverified)
- [ ] C-05 Sample layer amp envelope curve and length/trim
- [ ] C-06 Sample layer phase flip (180 degrees)
- [ ] C-07 Per-layer volume and routing to FX buses (see F-xx)
- [ ] C-08 Drag-and-drop a sample onto a layer
- [ ] C-09 Built-in click sample library browser (500 clicks in KICK 3; Kickforge ships synthesized clicks instead)
- [ ] C-10 Swap the top-end sample after AI import without touching the sub (see P-xx)

## F - FX
- [ ] F-01 FX section with 2 effect buses plus 1 master bus; each bus holds 2 effects
- [ ] F-02 Per-layer assignment of layers to bus A / bus B / master-only
- [ ] F-03 About 15 effects selectable in any slot (swappable)
- [ ] F-04 Distortion: Clipper
- [ ] F-05 Distortion: Tube
- [ ] F-06 Distortion: Wave (waveshaper)
- [ ] F-07 Distortion: Tape
- [ ] F-08 Distortion: Drive
- [ ] F-09 Distortion: Bit crush
- [ ] F-10 Complex waveshaper
- [ ] F-11 Brassify (harmonic-brightening effect; exact algorithm unverified)
- [ ] F-12 Ring modulator
- [ ] F-13 Delay
- [ ] F-14 Reverb
- [ ] F-15 Filter (type list unverified)
- [ ] F-16 EQ as an insertable effect
- [ ] F-17 Standard compressor
- [ ] F-18 1176-style (FET) compressor
- [ ] F-19 4 macro knobs, each controlling one effect parameter (mapping to arbitrary param, range unverified)
- [ ] F-20 Dedicated EQ tab: 2 parametric bands plus low shelf and high shelf
- [ ] F-21 EQ tab spectrum analyser (spectral display)
- [ ] F-22 Output limiter on master with 3 release settings
- [ ] F-23 Output limiter with 3 lookahead settings
- [ ] F-24 Limiter adds saturation when driven hard; input gain/ceiling controls (unverified)
- [ ] F-25 Oversampling / quality setting (not mentioned in any source; unverified)
- [ ] F-26 Master output gain (unverified)

## G - Global / MIDI
- [x] G-01 MIDI note triggers the kick; note number sets pitch where key tracking is on
- [x] G-02 Velocity affects level (mapping unverified)
- [x] G-03 Mono / polyphony behavior (unverified; plan: mono with declick, retrigger)
- [x] G-04 Glide / portamento (unverified; no source mentions it)
- [x] G-05 Pitch-bend response (unverified)
- [ ] G-06 Trigger tab: external sidechain input for drum replacement
- [ ] G-07 Trigger input filter
- [ ] G-08 Trigger Threshold
- [ ] G-09 Trigger Off Return (release threshold)
- [ ] G-10 Trigger Retrigger Return (re-arm hysteresis)
- [ ] G-11 Output MIDI notes to drive other instruments (single low-quality source; unverified)
- [x] G-12 Host automation of all parameters (users requested full automation exposure; unverified as present)
- [ ] G-13 Output routing: single stereo out (multi-out unverified)
- [ ] G-14 Formats: VST3 (AU/AAX out of scope for us)

## U - UI
- [ ] U-01 Resizable plugin window
- [ ] U-02 Node-based pitch curve editor: add, drag, delete nodes
- [ ] U-03 Node-based amp curve editor
- [ ] U-04 Bezier handle editing of segments
- [ ] U-05 Toggle log vs linear time view (log = zoomed-in attack; linear = amplitude work)
- [ ] U-06 Zoom and pan of curve view
- [ ] U-07 Undo / redo of edits
- [ ] U-08 Node context menu (e.g. right-click phase lock; selective key-track toggle)
- [ ] U-09 Waveform display of resulting sound
- [ ] U-10 Spectrum analyser in EQ tab
- [ ] U-11 Pitch readout in Hz and note name for fundamental/node under cursor (unverified)
- [ ] U-12 Tabs: Layers/sub, Effects, EQ, Trigger (as reported)
- [ ] U-13 Per-layer panels showing layer type, level and routing
- [ ] U-14 Macro knobs panel
- [ ] U-15 Audition button / play kick without MIDI (unverified)

## P - Presets / export
- [ ] P-01 Preset save/load/browser with categories (540+ presets in KICK 3)
- [ ] P-02 Global presets (whole kick) plus per-layer / sub-only / sample-selection presets
- [ ] P-03 Import and export preset files
- [ ] P-04 Load KICK 2 presets (compat; out of scope for Kickforge)
- [ ] P-05 Kick-to-WAV export (render current kick; drag-out to DAW unverified)
- [ ] P-06 Import-and-analyze: load a kick sample, estimate pitch/amp curves and harmonics, create a preset
- [ ] P-07 AI import recreates sub with the sub generator and places the top end in a sample layer as a trimmed section of the original
- [ ] P-08 Import accepts a longer loop; user highlights a single hit region to analyze
- [ ] P-09 Kick isolation from full tracks
- [ ] P-10 Randomization (explicitly NOT in KICK 3; optional extra)

## Our additions / deviations
- No third-party samples are shipped. In place of the 500-click library, Kickforge provides synthesized click generators: noise burst, impulse, filtered transient, short pitched blip, with tone/decay/filter controls (replaces C-09).
- Factory presets are entirely our own, authored for Kickforge. No KICK 2 compatibility (P-04) and no copies of Sonic Academy presets.
- AU and AAX are not built; VST3 and Standalone only (G-14).
- Optional extras we may add: preset randomizer / mutate (P-10), preset morph, built-in audition button (U-15).
- P-06 to P-09 (analysis import) may be phased late; a simple version (pitch-contour and amp-envelope extraction, no ML) is acceptable. Kick isolation (P-09) is stretch.
- Items marked "(unverified)" are design decisions for us; exact KICK 3 behavior should be checked against the manual or a trial copy (14-day free trial exists) before the Phase 3 parity pass.

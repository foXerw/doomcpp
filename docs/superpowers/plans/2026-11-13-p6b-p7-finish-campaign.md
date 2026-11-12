# P6b + P7 — Finish the Single-Player Campaign

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete doomcpp's single-player campaign: monster AI, all DOOM 1 monsters used by Freedoom's E1–E4, level progression with intermission, sound effects, and save/load.

**Architecture:** Keep the existing split — `doomcore` is SDL-free and unit-tested; `doomplatform` owns SDL. Enemy AI goes in a new `play/p_enemy.cpp` (vanilla p_enemy.c subset) and is dispatched from `P_SetMobjState(World&)`. Campaign flow lives in `play/g_game.cpp` + `ui/f_font.cpp`/`ui/f_intermission.cpp`. Audio is a `platform/i_sound.cpp` backend reached through a core function-pointer hook so `doomcore` tests still link. Save/load lives in `play/g_save.cpp` and serializes the World, not the map.

**Tech Stack:** C++17, CMake, SDL2 (video + audio), doctest, Freedoom IWAD.

**Spec:** `docs/superpowers/specs/2026-07-19-doom-clone-design.md` + `docs/superpowers/specs/2026-07-25-roadmap-realignment-design.md` (P6/P7 sections).

## Global Constraints

- Collision/movement math is 16.16 fixed-point; the renderer stays float (documented permanent deviation).
- `doomcore` must never link SDL. Platform hooks use function pointers.
- Function/type names stay in vanilla PascalCase (`P_CheckSight`, `A_Chase`).
- Every feature gets a doctest test; runtime paths also get a `--smoketest` assertion.
- Each phase ends on a feature branch merged `--no-ff` to `main` with a tag.
- Freedoom IWAD only (no commercial assets).

## Review Focus

- A monster must not shoot through a closed door or one-sided wall (`P_CheckSight`).
- Monsters must not walk off ledges (dropoff check) and must not block each other permanently.
- A dead/removed monster must never remain a target (null / health<=0 checks in A_Chase).
- Level transition must preserve inventory and must not keep stale mobjs/movers from the old map.
- Save/load must round-trip a partially-open door and a live projectile without dangling pointers.

---

### Task 1: Monster table expansion (data only)

**Files:** `src/play/info.h`, `src/play/info.cpp`, `tests/test_info.cpp`
**Interfaces produced:** new `spritenum_t` entries `SPR_BAL1/2/7, SPR_SKUL, SPR_HEAD, SPR_BOSS, SPR_CYBR, SPR_SPID`; new `statenum_t` groups listed below; new `mobjtype_t` `MT_TROOPSHOT, MT_HEADSHOT, MT_BRUISERSHOT, MT_SKULL, MT_HEAD, MT_BRUISER, MT_CYBORG, MT_SPIDER, MT_SHADOWS`; new actions `A_BossDeath, A_BruisAttack, A_CyberAttack, A_HeadAttack, A_SkullAttack, A_SpidAttack`.

- [ ] Add sprites/states/mobjinfo for: imp fireball (`BAL1`), caco fireball (`BAL2`), baron ball (`BAL7`), lost soul (`SKUL`), cacodemon (`HEAD`), baron (`BOSS`), cyberdemon (`CYBR`), spider mastermind (`SPID`), spectre (`MT_SHADOWS` reuses `S_SARG_*`).
- [ ] `static_assert` table/enum sync already exists — keep it.
- [ ] Test: `mobjtypeForDoomednum(3006)==MT_SKULL`, `3005==MT_HEAD`, `3003==MT_BRUISER`, `16==MT_CYBORG`, `7==MT_SPIDER`, `58==MT_SHADOWS`; every monster's `seestate/painstate/deathstate` is a live state; projectile `deathstate != S_NULL`.
- [ ] Test: all new state sprite indices are valid and sprite names exist in `sprnames[]`.

### Task 2: mobj fields + damage targeting

**Files:** `src/play/p_mobj.h`, `src/play/p_mobj.cpp`, `src/play/p_inter.cpp`, `tests/test_p_mobj.cpp`, `tests/test_p_attack.cpp`
**Interfaces produced:** `mobj_t::reactiontime`, `mobj_t::moveDir` (int), `mobj_t::threshold` (int, used for target switching); `P_DamageMobj` sets `target.target = source`, `reactiontime = 0`, `flags |= MF_JUSTHIT` for monsters.

- [ ] Spawn initializes `reactiontime = info->reactiontime`.
- [ ] Damage from a different source sets the victim's target; does not retarget the player.
- [ ] Add dropoff gate to `P_TryMove` (World + raw variants): `!(flags & MF_DROPOFF) && floorz - oldfloorz < -STEP_LIMIT` fails.
- [ ] Tests: monster knocked back targets its attacker; monster refuses to step off a 64-unit ledge; player (MF_DROPOFF) still can.

### Task 3: Enemy AI core (`play/p_enemy.cpp`)

**Files:** create `src/play/p_enemy.h`, `src/play/p_enemy.cpp`; modify `src/play/p_mobj.cpp`, `src/play/p_tick.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`
**Interfaces produced:**
- `bool P_CheckSight(const World&, const mobj_t&, const mobj_t&)`
- `bool P_LookForPlayers(World&, mobj_t&)`
- `void P_NoiseAlert(World&, mobj_t* source)`
- `void P_MobjAction(World&, mobj_t&, action_t)` — dispatch for A_Look/A_Chase/attacks/screams/falls/BossDeath; `P_DoMobjAction` delegates here.
- `P_SetupLevel` computes `World::totalKills/totalItems`, stores `World::mapName`.

- [ ] `P_CheckSight`: 2D segment vs every linedef; one-sided or closed opening at eye level blocks. Behind-closed-door test.
- [ ] `A_Look` → `P_LookForPlayers` → target + `seestate`. Range 1800, sight required, 180° FOV (MF_AMBUSH exempt).
- [ ] `A_Chase`: reactiontime stall → validate target → sight check → move toward target with `P_Move` (full step, then ±45°/±90° slide) → melee/missile decision.
- [ ] Attacks: `A_FaceTarget`, `A_PosAttack`, `A_SPosAttack`, `A_SargAttack`, `A_TroopAttack` (spawn `MT_TROOPSHOT`), `A_HeadAttack` (`MT_HEADSHOT`), `A_BruisAttack` (`MT_BRUISERSHOT` or melee), `A_CyberAttack` (`MT_ROCKET`), `A_SpidAttack` (3 hitscan), `A_SkullAttack` (charge), `A_BossDeath` (special 666 floors lower once all same-type bosses die).
- [ ] `A_Pain`/`A_Scream`/`A_XScream`/`A_PlayerScream`/`A_Fall` call the sound hook and clear MF_SOLID on fall.
- [ ] `P_MobjThinker`: MF_SKULLFLY path (move until blocked/touched, then damage or seestate).
- [ ] Spawning: flying monsters (`MF_FLOAT`) start 24 units above the floor.
- [ ] Tests in `tests/test_p_enemy.cpp`: waking in/out of sight; chase moves closer; possessed hits the player for damage; imp fireball spawns and travels; demon bite; lost soul charge damages; infighting; boss death lowers tag-666 floor; dead target does not crash.
- [ ] `--smoketest`: synthetic monster 96 units in front, wake it, require it to move and/or damage the player.

### Task 4: Player noise + attack feedback

**Files:** `src/play/p_pspr.cpp`, `src/play/p_map.cpp`, `src/play/p_enemy.cpp`, tests
- [ ] Firing a weapon calls `P_NoiseAlert(w, player.mo)`; nearby awake-capable monsters with sight get the player as target.
- [ ] Test: a monster behind the player wakes after a pistol shot (sight permitting).

### Task 5: Campaign flow (P7a)

**Files:** create `src/play/g_game.h/.cpp`; modify `src/play/p_spec.cpp`, `src/play/p_tick.h`, `src/play/p_tick.cpp`, `CMakeLists.txt`, tests
**Interfaces produced:**
- `std::string G_NextLevelName(const std::string& cur, bool secretExit);` (`E1M8`→`E2M1` … `E4M8`→`""`)
- `void G_DoCompleted(World&)` / `World::levelExitRequested/secretExit`
- `P_SetupLevel(World&, const MapData&, const player_t* carry)` — carry inventory across levels; fresh pistol start when null.

- [ ] Line specials 11/52 normal exit, 51/124 secret exit set the request flags.
- [ ] `G_NextLevelName` implements M1..M8, M8→next episode M1, M3 secret→M9, M9→M4, E4M8→"".
- [ ] `P_SetupLevel` carry overload preserves health/ammo/weapons/armor/keys/readyweapon; resets kill/item counts and mobjs/movers.
- [ ] Tests: exit mapping incl. secret and episode boundaries; inventory carry; stale mobjs/movers cleared.

### Task 6: Intermission screen + font (P7a)

**Files:** create `src/ui/f_font.h/.cpp`, `src/ui/f_intermission.h/.cpp`; modify `src/ui/st_stuff.*`? no; `CMakeLists.txt`, tests
**Interfaces produced:** `Font::Font(const WadFile&)`, `Font::drawText(fb,w,h,x,y,text)`; `Intermission::draw(fb,w,h, level, killsPct, itemsPct, secretsPct, nextLevel)`.
- [ ] STCFN033..095 patch font, lowercase→uppercase, space, `%`, `-`, `:`, `.`.
- [ ] Intermission: `E1M1 FINISHED`, `KILLS xx%`, `ITEMS xx%`, `SECRETS xx%`, `ENTERING E1M2`, `PRESS ENTER`.
- [ ] Tests: text width/height nonzero; drawing marks pixels; unknown glyph skipped.

### Task 7: Audio backend (P7b)

**Files:** create `src/platform/i_sound.h/.cpp`; modify `src/core/i_system.h/.cpp`, `src/play/*.cpp`, `src/main.cpp`, `CMakeLists.txt`, tests
**Interfaces produced:** `enum sfxnum_t` in `src/play/sfx.h`; `void I_StartSound(int id, int vol)`; `void I_SetSoundHook(void(*)(int,int))` in core; `i_sound::init/loadWad/shutdown` in platform.
- [ ] Core hook: `doomcore` calls a function pointer, never SDL; tests get a no-op.
- [ ] Backend: SDL_OpenAudioDevice 22050 Hz mono S16, 8 channels, decodes `DS*` 8-bit unsigned PCM with the 8-byte Doom header, mixes and clips.
- [ ] Wire sounds: pistol/shotgun/chaingun/launcher/plasma/BFG, punch/saw, door open/close, switch, lift, teleport, item pickup, weapon up, monster sight/attack/pain/death, player pain/death, barrel explosion.
- [ ] Tests: hook receives ids when firing/opening a door; DS header decode unit test (header parsing in core).

### Task 8: Save / load (P7c)

**Files:** create `src/play/g_save.h/.cpp`; modify `src/platform/i_video.*` (F5/F9), `src/main.cpp`, `CMakeLists.txt`, tests
**Interfaces produced:** `bool G_SaveGame(const World&, const MapData&, const std::string& path)`; `bool G_LoadGame(World&, const MapData&, const std::string& path)`; `World::mapName`.
- [ ] Binary format v1: magic `DCPS`, map name, tic, sector heights, movers, player inventory/psprites, mobj array (type, pos, angle, mom, health, flags, state index, reactiontime, target index).
- [ ] Load rebuilds mobjs via `P_SpawnMobj`, restores fields, resolves target/player pointers by index, refreshes render cache without running actions.
- [ ] F5 quick-saves `doomcpp-quicksave.dat`; F9 loads it (reloading the saved map first).
- [ ] Tests: round-trip player health/ammo/keys; partially open door height preserved; live projectile preserved with momentum; corrupted magic returns false.

### Task 9: Game loop integration + campaign polish (P7d)

**Files:** `src/main.cpp`, `src/platform/i_video.*`, `src/play/p_tick.cpp`, `README.md`
- [ ] CLI: `--warp E#M#` (default E1M1), `--god` debug, `--nosound`.
- [ ] Game state machine in main: playing → intermission → next level / victory; death → death screen → Enter restarts current level with pistol start.
- [ ] Preserve player inventory across levels; level title in the window title.
- [ ] `--smoketest` additions: monster AI, exit→intermission mapping, save/load round-trip.
- [ ] Full-campaign headless smoke: `--campaigntest` walks E1M1..E1M8/E1M9 by loading each map and spawning all mobjs (no play), asserting map load + spawn + a few tics for every map of all 4 episodes.
- [ ] README: P6b/P7 status, controls (F5/F9/Enter), known limitations (music not implemented; spectre drawn opaque; simplified stair/plat speeds).

### Task 10: Final verification

- [ ] `cmake --build build --config Release` clean.
- [ ] `ctest` all green (doomcpp_tests + runtime_smoke).
- [ ] `--smoketest` passes.
- [ ] `--gapscan` geometry=0 at several poses.
- [ ] `--dumpframe` visual check of E1M2/E1M7 with monsters.
- [ ] Merge `feat/p6b-enemy-ai` → `main` (`--no-ff`), tag `v0.14-p7-campaign`; update memory + README.

## Self-Review Notes

- Spec coverage: P6b (p_enemy/p_sight/actions/infighting) → Tasks 1–4; P7 (flow/audio/save) → Tasks 5–9.
- Known intentional limits: MUS music is not implemented (no OPL synth) — SFX only; secrets are not counted (sector special 9 not tracked); intermission uses the status font.
- No task adds networking (out of roadmap).

## Result (2026-11-13)

Implemented and verified on `feat/p6b-enemy-ai`:

- All 10 tasks complete. Tests: **185/185** (unit + runtime/AI/campaign/sound smoke).
- New runtime gates: `--smoketest` (AI wake assertion + new-sprite decode), `--aittest`
  (all six monster families wake, damage the player, fire projectiles, close distance),
  `--campaigntest` (all 36 E1–E4 maps load/tick, every exit line sets the request,
  campaign chain, save/load round-trip), `--soundtest` (67/67 SFX decode, 32 MIDI
  tracks parse, synth RMS > 200).
- Deviations discovered during implementation:
  - Freedoom music is Standard MIDI (`MThd`), not MUS — implemented a small SMF
    parser + sine/octave synth instead of an OPL/MUS player.
  - The P4 sprite occlusion limitation (per-column `wallDepth`) turned out to hide
    nearly every sprite behind a low floor riser at the E1M1 start. Replaced with a
    per-pixel wall-depth buffer (also fixes windows/grates).
  - Player momentum/friction/view bob and Shift-run were backfilled (vanilla
    forwardmove/sidemove thrust + P_XYMovement friction).
  - Monsters open doors when their chase is blocked.
- Known limitations: no menus/title, static HUD face, WI graphics absent, simplified
  monster pathing/stairs, no crushers/light-flicker, synth timbre is not OPL-faithful.

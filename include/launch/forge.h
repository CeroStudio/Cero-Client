#ifndef CERO_LAUNCH_FORGE_H
#define CERO_LAUNCH_FORGE_H

#include <stddef.h>
#include "launch_ctx.h"
#include "../utils/version_manifest.h"

/*
 * Forge support covers two eras:
 *
 * - Legacy (1.6 - 1.12.2): the installer jar embeds the full launch
 *   profile directly as install_profile.json -> "versionInfo" (no
 *   install-time processing needed). Forge itself runs as a regular
 *   classpath jar under net.minecraft.launchwrapper.Launch, selected via
 *   a "--tweakClass <FMLTweaker>" argument extracted from the profile's
 *   "minecraftArguments" string.
 *
 * - Modern (1.13+): the installer + processors method. It downloads the
 *   real Forge installer jar, extracts its install_profile.json + merged
 *   version.json, downloads every library it lists, then re-executes
 *   each "processor" step (the same steps the official installer GUI
 *   runs) via plain `java -cp ... MainClass args...` calls.
 *
 * Neither path involves the Cero/Forge agent.
 *
 * NOTE: true pre-1.6 Forge (1.1 - 1.5.2) used raw binary .lzma
 * "binpatches" applied directly to the vanilla client jar bytes, a
 * completely different (and, today, extremely rarely used) mechanism.
 * That path is not implemented.
 */

int parse_forge_spec(const char* spec, char* mc, size_t mcsz,
                     char* loader, size_t loadersz);

/*
 * NeoForge is a fork of Forge (split off at Minecraft 1.20.1) that keeps
 * the exact same installer + processors mechanism, so it reuses every
 * helper in forge.c (library resolution, data-token substitution,
 * processor execution). What differs is only:
 * - its own maven (maven.neoforged.net) and artifact coordinates
 *   (net.neoforged:neoforge instead of net.minecraftforge:forge)
 * - no "recommended" build concept: picking a version means fetching the
 *   release list and taking the latest one matching the Minecraft
 *   version's "<major>.<minor>" NeoForge-version prefix.
 *
 * NOTE: MC 1.20.1 shipped a transitional NeoForge release still using
 * Forge's own "net.neoforged:forge:1.20.1-<ver>" naming; that one-off
 * compat case is not handled here yet (use fetch_forge_profile for it
 * in the meantime, since it is artifact-compatible with Forge itself).
 */
int parse_neoforge_spec(const char* spec, char* mc, size_t mcsz,
                        char* loader, size_t loadersz);

int fetch_neoforge_profile(const LaunchCtx* ctx, const char* client_dir,
                          const char* mc_version, const char* neoforge_version_in,
                          const char* java_exe,
                          const char* vanilla_jar, const char* vanilla_json,
                          char* out_id, size_t out_id_sz,
                          char* out_json_path, size_t out_json_sz);

/* Resolves the Forge version to use, downloads + extracts the installer,
 * downloads every install-time library, runs every install processor,
 * and finally writes/normalizes the launch version json.
 *
 * java_exe / vanilla_jar / vanilla_json are needed to run the processors
 * (they patch the vanilla client jar to produce the "srg" mapped jar
 * Forge itself launches from).
 *
 * out_id:        version id to use for the rest of the launch pipeline
 *                (e.g. "1.20.1-forge-47.2.20")
 * out_json_path: path to that version's launch json on disk
 */
int fetch_forge_profile(const LaunchCtx* ctx, const char* client_dir,
                        const char* mc_version, const char* forge_version_in,
                        const char* java_exe,
                        const char* vanilla_jar, const char* vanilla_json,
                        char* out_id, size_t out_id_sz,
                        char* out_json_path, size_t out_json_sz);

/* Extracts extra loader-specific JVM/game arguments a Forge version json
 * needs beyond the generic vanilla/fabric set build_launch_argv() already
 * builds:
 * - legacy (<=1.12.2): the "--tweakClass X" pair pulled out of the old
 *   flat "minecraftArguments" string.
 * - modern (1.13-1.16.5): every plain string entry of "arguments"."game"
 *   or "arguments"."jvm" (feature-conditional object entries, used for
 *   things like --demo/--width/--height, are skipped).
 *
 * `kind` is "game" or "jvm". Returned pointers alias `version_json`'s own
 * strings — keep it loaded for as long as the results are used. Returns
 * the number of entries written to out_args. */
int forge_extract_extra_args(VmJVal* version_json, const char* kind,
                             const char** out_args, int max);


void forge_resolve_placeholder(const char* client_dir, const char* classpath,
                               const char* natives_dir, const char* version_name,
                               const char* raw, char* out, size_t outsz);

#endif
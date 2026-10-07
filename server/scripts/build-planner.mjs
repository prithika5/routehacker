// Compiles the C++ route planner (bin/routeplanner_web) when the server's
// dependencies are installed. Render's Node service runs `npm install` in
// server/, so this is what puts the real planner on the live API.
// It never fails the install: if the toolchain or sources aren't present
// (for example in the Docker runtime stage), it explains and skips.
import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

if (process.env.SKIP_PLANNER_BUILD) {
  console.log("[planner] SKIP_PLANNER_BUILD is set; not compiling the C++ planner.");
  process.exit(0);
}

if (!existsSync(path.join(repoRoot, "Makefile")) || !existsSync(path.join(repoRoot, "src"))) {
  console.log("[planner] C++ sources not found next to the server; skipping planner build.");
  process.exit(0);
}

const run = (args) => spawnSync("make", ["-C", repoRoot, ...args], { stdio: "inherit" });

const prepared = run(["directories"]);
const built = prepared.status === 0 ? run(["bin/routeplanner_web"]) : prepared;

if (built.error || built.status !== 0) {
  console.warn(
    "[planner] Could not compile the C++ planner (needs make, g++ and libexpat). " +
      "The API will answer CPP_PLANNER_MISSING until it is built."
  );
  process.exit(0);
}

console.log("[planner] Built bin/routeplanner_web");

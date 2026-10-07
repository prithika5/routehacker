import { createApp } from "./app.js";
import { computeCppRoute } from "./services/cppPlannerService.js";

const port = Number(process.env.PORT || 3000);
const app = createApp();

// Runs one real route at boot so the logs show whether the C++ planner works on this host.
async function checkPlanner() {
  const startedAt = Date.now();
  try {
    const route = await computeCppRoute({ start: "aggie_works", end: "west_village", optimization: "fastest" });
    console.log(`C++ planner ready: sample route ${route.totals.time}, ${route.totals.distance} in ${Date.now() - startedAt} ms`);
  } catch (error) {
    const code = error?.body?.error?.code || error?.message || "unknown error";
    console.warn(`C++ planner unavailable (${code}); route requests will fail until it is built.`);
  }
}

app.listen(port, () => {
  console.log(`RouteHacker API listening on http://localhost:${port}`);
  if (process.env.NODE_ENV !== "test") {
    checkPlanner();
  }
});

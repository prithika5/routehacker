const apiBaseUrl = (import.meta.env.VITE_API_BASE_URL || "").replace(/\/$/, "");

// Messages people see when the route service itself has trouble. Anything
// else (for example "no connected route") is shown as the server wrote it.
const friendlyMessages = {
  CPP_PLANNER_TIMEOUT: "The route took too long to calculate. Try again in a moment.",
  CPP_PLANNER_FAILED: "The route planner hit a problem with this trip. Try again, or pick different places.",
  CPP_PLANNER_MISSING: "The route planner isn't running on the server right now. Try again later.",
  NETWORK: "Can't reach the route server. Check your connection and try again."
};

export class RouteError extends Error {
  constructor(code, message) {
    super(friendlyMessages[code] || message || "Something went wrong while finding a route. Try again.");
    this.name = "RouteError";
    this.code = code;
  }
}

export async function requestRoute(payload, options = {}) {
  let response;

  try {
    response = await fetch(`${apiBaseUrl}/api/route`, {
      method: "POST",
      headers: {
        "Content-Type": "application/json"
      },
      body: JSON.stringify(payload),
      signal: options.signal
    });
  } catch (error) {
    if (error?.name === "AbortError") {
      throw error;
    }
    throw new RouteError("NETWORK");
  }

  let data = null;
  try {
    data = await response.json();
  } catch {
    data = null;
  }

  if (!response.ok) {
    throw new RouteError(data?.error?.code, data?.error?.message);
  }

  return data;
}

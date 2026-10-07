import { cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";
import App from "./App.jsx";
import { describeStep } from "./components/RouteResults.jsx";

afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
});

const successfulRoute = {
  summary: "AggieWorks Studio to West Village",
  engine: "cpp",
  optimization: "fastest",
  modePreference: "any",
  totals: { distance: "2.95 mi", time: "10 min", rawDistance: 2.95, rawTime: 10 },
  geometry: {
    type: "LineString",
    coordinates: [
      [-121.7407, 38.5445],
      [-121.7597, 38.5382],
      [-121.7718, 38.5442]
    ]
  },
  breakdown: [
    { mode: "walk", label: "Walk", stepCount: 1, distance: "0.06 mi", time: "1 min" },
    { mode: "shuttle", label: "Shuttle", stepCount: 1, distance: "2.89 mi", time: "6 min" }
  ],
  steps: [
    { index: 1, mode: "walk", distance: "0.06 mi", time: "1 min", instruction: "Walk W along 3rd Street for 319 ft" },
    { index: 2, mode: "shuttle", distance: "", time: "", instruction: "Ride Buses X, V" },
    { index: 3, mode: "walk", distance: "0.30 mi", time: "6 min", instruction: "Walk for 0.30 mi" }
  ]
};

describe("RouteHacker app", () => {
  it("renders the planner shell", () => {
    render(<App />);

    expect(screen.getByAltText("RouteHacker")).toBeInTheDocument();
    expect(screen.getByLabelText("From")).toBeInTheDocument();
    expect(screen.getByLabelText("To")).toBeInTheDocument();
    expect(screen.getByRole("radio", { name: /Fastest/i })).toHaveAttribute("aria-checked", "true");
    expect(screen.getByRole("button", { name: /Find route/i })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: /Where to\?/i })).toBeInTheDocument();
  });

  it("validates same start and destination before calling the API", async () => {
    const fetchSpy = vi.spyOn(globalThis, "fetch");
    render(<App />);

    fireEvent.focus(screen.getByLabelText("To"));
    fireEvent.change(screen.getByLabelText("To"), { target: { value: "Aggie" } });
    fireEvent.click(screen.getByRole("option", { name: /AggieWorks Studio/i }));
    fireEvent.click(screen.getByRole("button", { name: /Find route/i }));

    expect(await screen.findByText(/Pick two different places/i)).toBeInTheDocument();
    expect(fetchSpy).not.toHaveBeenCalled();
  });

  it("supports keyboard selection in the place search", () => {
    render(<App />);
    const input = screen.getByLabelText("From");

    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "library" } });
    fireEvent.keyDown(input, { key: "Enter" });

    expect(input).toHaveValue("Shields Library");
  });

  it("swaps start and destination", () => {
    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: /Swap start and destination/i }));

    expect(screen.getByLabelText("From")).toHaveValue("West Village");
    expect(screen.getByLabelText("To")).toHaveValue("AggieWorks Studio");
  });

  it("renders the trip summary and strip-map directions after a successful request", async () => {
    vi.spyOn(globalThis, "fetch").mockResolvedValueOnce({ ok: true, json: async () => successfulRoute });

    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: /Find route/i }));

    expect(screen.getAllByText(/Finding route/i).length).toBeGreaterThan(0);

    expect(await screen.findByRole("heading", { name: /AggieWorks Studio to West Village/i })).toBeInTheDocument();
    expect(screen.getByText("10 min")).toBeInTheDocument();
    expect(screen.getByText("2.95 mi")).toBeInTheDocument();
    expect(screen.getByText("On the bus")).toBeInTheDocument();
    expect(screen.getByText("Walk west on 3rd Street")).toBeInTheDocument();
    expect(screen.getByText(/Ride bus X, then transfer to V/)).toBeInTheDocument();
    expect(screen.getByText("Walk to your destination")).toBeInTheDocument();
  });

  it("shows server messages for route problems", async () => {
    vi.spyOn(globalThis, "fetch").mockResolvedValue({
      ok: false,
      json: async () => ({
        error: { code: "NO_ROUTE", message: "No connected route exists for the requested locations." }
      })
    });

    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: /Find route/i }));

    await waitFor(() => {
      expect(screen.getByText(/No connected route exists/i)).toBeInTheDocument();
    });
    expect(screen.getByRole("heading", { name: /Couldn't get a route/i })).toBeInTheDocument();
  });

  it("replaces planner internals with plain-language errors", async () => {
    vi.spyOn(globalThis, "fetch").mockResolvedValue({
      ok: false,
      json: async () => ({
        error: { code: "CPP_PLANNER_TIMEOUT", message: "The C++ route planner took too long to respond." }
      })
    });

    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: /Find route/i }));

    expect(await screen.findByText(/The route took too long to calculate/i)).toBeInTheDocument();
    expect(screen.queryByText(/C\+\+/)).not.toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Try again/i })).toBeInTheDocument();
  });

  it("explains when the server can't be reached", async () => {
    vi.spyOn(globalThis, "fetch").mockRejectedValue(new TypeError("Failed to fetch"));

    render(<App />);
    fireEvent.click(screen.getByRole("button", { name: /Find route/i }));

    expect(await screen.findByText(/Can't reach the route server/i)).toBeInTheDocument();
  });
});

describe("describeStep", () => {
  it("turns planner instructions into plain directions", () => {
    expect(describeStep({ instruction: "Walk W along 3rd Street for 319 ft" }, false)).toMatchObject({
      text: "Walk west on 3rd Street",
      length: "319 ft"
    });
    expect(describeStep({ instruction: "Walk NW toward End for 493 ft" }, true).text).toBe("Walk northwest to your destination");
    expect(describeStep({ instruction: "Bike for 0.32 mi" }, false).text).toBe("Bike 0.32 mi");
    expect(describeStep({ instruction: "Ride Bus E" }, false)).toMatchObject({ text: "Ride bus E", lines: ["E"] });
    expect(describeStep({ instruction: "Ride Buses D, Q" }, false).lines).toEqual(["D", "Q"]);
    expect(describeStep({ instruction: "Shuttle via Campus express" }, false).text).toBe("Shuttle via Campus express");
  });
});

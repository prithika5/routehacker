import { useEffect, useRef } from "react";

const modeNames = {
  walk: "Walk",
  bike: "Bike",
  shuttle: "Bus"
};

const breakdownNames = {
  walk: "Walking",
  bike: "Biking",
  shuttle: "On the bus"
};

const compass = {
  N: "north",
  S: "south",
  E: "east",
  W: "west",
  NE: "northeast",
  NW: "northwest",
  SE: "southeast",
  SW: "southwest"
};

// Turns planner text ("Walk W along 3rd Street for 319 ft", "Ride Buses X, V")
// into plain directions plus the route letters to badge.
export function describeStep(step, isLast) {
  const instruction = (step.instruction || "").trim();

  const bus = instruction.match(/^(?:Ride|Take)\s+Bus(?:es)?\s+([A-Z0-9]+(?:\s*,\s*[A-Z0-9]+)*)/i);
  if (bus) {
    const lines = bus[1].split(",").map((line) => line.trim());
    const text =
      lines.length === 1
        ? `Ride bus ${lines[0]}`
        : `Ride bus ${lines[0]}, then transfer to ${lines.slice(1).join(", then ")}`;
    return { text, lines, length: step.distance };
  }

  const along = instruction.match(/^(Walk|Bike)\s+([NSEW]{1,2})\s+(along|toward)\s+(.+?)\s+for\s+(.+)$/);
  if (along) {
    const [, verb, direction, relation, place, length] = along;
    const heading = compass[direction] || direction;
    const target = relation === "toward" && /^end$/i.test(place) ? "to your destination" : `${relation === "along" ? "on" : "toward"} ${place}`;
    return { text: `${verb} ${heading} ${target}`, lines: [], length };
  }

  const plain = instruction.match(/^(Walk|Bike)\s+for\s+(.+)$/);
  if (plain) {
    return { text: isLast ? `${plain[1]} to your destination` : `${plain[1]} ${plain[2]}`, lines: [], length: plain[2] };
  }

  return { text: instruction, lines: [], length: step.distance };
}

function Notice({ title, children, tone = "neutral", action }) {
  return (
    <div className={`notice ${tone}`} role={tone === "error" ? "alert" : undefined}>
      <h2>{title}</h2>
      <p>{children}</p>
      {action}
    </div>
  );
}

function LoadingState() {
  return (
    <div className="result-loading" aria-hidden="true">
      <div className="skeleton skeleton-time" />
      <div className="skeleton skeleton-line" />
      <div className="skeleton-strip">
        <div className="skeleton skeleton-line" />
        <div className="skeleton skeleton-line short" />
        <div className="skeleton skeleton-line" />
      </div>
    </div>
  );
}

export default function RouteResults({ route, error, loading, startLocation, endLocation, onRetry }) {
  const resultRef = useRef(null);

  // On phones the result sits below the form; bring it into view when it arrives.
  useEffect(() => {
    if (!route || !resultRef.current || globalThis.innerWidth >= 900) {
      return;
    }
    const reduceMotion = globalThis.matchMedia?.("(prefers-reduced-motion: reduce)").matches;
    resultRef.current.scrollIntoView?.({ behavior: reduceMotion ? "auto" : "smooth", block: "start" });
  }, [route]);

  if (loading) {
    return (
      <section className="trip-result" aria-busy="true" aria-label="Route">
        <p className="visually-hidden" role="status">
          Finding route…
        </p>
        <LoadingState />
      </section>
    );
  }

  if (error) {
    return (
      <section className="trip-result" aria-label="Route">
        <Notice
          tone="error"
          title="Couldn't get a route"
          action={
            onRetry ? (
              <button type="button" className="text-button" onClick={onRetry}>
                Try again
              </button>
            ) : null
          }
        >
          {error}
        </Notice>
      </section>
    );
  }

  if (!route) {
    return (
      <section className="trip-result" aria-label="Route">
        <Notice title="Where to?">
          Choose where you're starting and where you're headed. Fastest mixes walking, biking and campus buses; shortest
          keeps you on foot.
        </Notice>
      </section>
    );
  }

  const breakdown = (route.breakdown || []).filter((entry) => entry.time);

  return (
    <section ref={resultRef} className="trip-result" aria-label="Route" aria-live="polite">
      <header className="result-head">
        <p className="result-kind">{route.optimization === "fastest" ? "Fastest route" : "Shortest route"}</p>
        <p className="result-time">{route.totals.time}</p>
        <h2 className="result-title">
          {startLocation?.label} to {endLocation?.label}
        </h2>
        <dl className="result-facts">
          <div>
            <dt>Distance</dt>
            <dd>{route.totals.distance}</dd>
          </div>
          {breakdown.map((entry) => (
            <div key={entry.mode}>
              <dt>{breakdownNames[entry.mode] || entry.label}</dt>
              <dd>{entry.time}</dd>
            </div>
          ))}
        </dl>
      </header>

      <ol className="strip" aria-label="Directions">
        <li className="strip-stop origin">
          <span className="strip-pin" aria-hidden="true">
            <span>A</span>
          </span>
          <strong>{startLocation?.label}</strong>
        </li>

        {route.steps.map((step, index) => {
          const { text, lines, length } = describeStep(step, index === route.steps.length - 1);
          const mode = modeNames[step.mode] ? step.mode : "walk";
          return (
            <li key={step.index} className={`strip-leg ${mode}`}>
              <span className="strip-track" aria-hidden="true" />
              <div className="strip-body">
                <p className="strip-text">
                  {lines.length ? (
                    <span className="bus-lines" aria-hidden="true">
                      {lines.map((line) => (
                        <span key={line} className="bus-badge">
                          {line}
                        </span>
                      ))}
                    </span>
                  ) : null}
                  {text}
                </p>
                <p className="strip-meta">
                  <span>{modeNames[step.mode] || step.mode}</span>
                  {length ? <span>{length}</span> : null}
                  {step.time ? <span>{step.time}</span> : null}
                </p>
              </div>
            </li>
          );
        })}

        <li className="strip-stop destination">
          <span className="strip-pin" aria-hidden="true">
            <span>B</span>
          </span>
          <strong>{endLocation?.label}</strong>
        </li>
      </ol>
    </section>
  );
}

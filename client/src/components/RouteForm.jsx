import { optimizationModes } from "@shared/routeOptions.js";
import LocationSearchField from "./LocationSearchField.jsx";

const orderedModes = [...optimizationModes].sort((a, b) => (a.id === "fastest" ? -1 : b.id === "fastest" ? 1 : 0));

const modeCopy = {
  fastest: { label: "Fastest", hint: "Walk, bike and bus" },
  shortest: { label: "Shortest", hint: "Fewest miles on foot" }
};

export default function RouteForm({
  startLocation,
  endLocation,
  locations,
  optimization,
  loading,
  validationError,
  onLocationSelect,
  onOptimizationChange,
  onSwap,
  onSubmit
}) {
  return (
    <section className="trip-form" aria-label="Plan a trip">
      <header className="brand">
        <img src="/logo-lockup.png" alt="RouteHacker" className="brand-lockup" width="240" height="88" />
        <p className="brand-tagline">Getting around UC Davis</p>
      </header>

      <form className="planner-form" onSubmit={onSubmit}>
        <div className="place-pair">
          <LocationSearchField
            label="From"
            role="start"
            value={startLocation}
            locations={locations}
            onSelect={(locationId) => onLocationSelect("start", locationId)}
          />
          <LocationSearchField
            label="To"
            role="end"
            value={endLocation}
            locations={locations}
            onSelect={(locationId) => onLocationSelect("end", locationId)}
          />
          <button type="button" className="swap-button" onClick={onSwap} aria-label="Swap start and destination" title="Swap">
            <svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true">
              <path
                d="M7 4v14m0 0-3.5-3.5M7 18l3.5-3.5M17 20V6m0 0-3.5 3.5M17 6l3.5 3.5"
                fill="none"
                stroke="currentColor"
                strokeWidth="2"
                strokeLinecap="round"
                strokeLinejoin="round"
              />
            </svg>
          </button>
        </div>

        <div className="mode-toggle" role="radiogroup" aria-label="Route type">
          {orderedModes.map((mode) => {
            const copy = modeCopy[mode.id] || { label: mode.label, hint: "" };
            const selected = optimization === mode.id;
            return (
              <button
                key={mode.id}
                type="button"
                role="radio"
                aria-checked={selected}
                className={selected ? "selected" : ""}
                onClick={() => onOptimizationChange(mode.id)}
              >
                <strong>{copy.label}</strong>
                <span>{copy.hint}</span>
              </button>
            );
          })}
        </div>

        {validationError ? (
          <p className="form-error" role="alert">
            {validationError}
          </p>
        ) : null}

        <button type="submit" className="find-button" disabled={loading}>
          {loading ? <span className="button-spinner" aria-hidden="true" /> : null}
          {loading ? "Finding route…" : "Find route"}
        </button>
      </form>
    </section>
  );
}

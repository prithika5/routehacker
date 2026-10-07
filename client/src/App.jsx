import { startTransition, useMemo, useRef, useState } from "react";
import { getLocationOptionById, locationOptions } from "@shared/routeOptions.js";
import MapView from "./components/MapView.jsx";
import RouteForm from "./components/RouteForm.jsx";
import RouteResults from "./components/RouteResults.jsx";
import { requestRoute } from "./lib/api.js";

const defaultForm = {
  start: "aggie_works",
  end: "west_village",
  optimization: "fastest",
  modePreference: "any"
};

export default function App() {
  const activeLocations = useMemo(() => locationOptions.filter((location) => location.status === "active"), []);
  const [formState, setFormState] = useState(defaultForm);
  const [route, setRoute] = useState(null);
  const [routeForm, setRouteForm] = useState(null);
  const [error, setError] = useState("");
  const [validationError, setValidationError] = useState("");
  const [loading, setLoading] = useState(false);
  const cacheRef = useRef(new Map());
  const requestRef = useRef(null);

  const startLocation = getLocationOptionById(formState.start);
  const endLocation = getLocationOptionById(formState.end);
  // Label the result with the places it was computed for, even if the form changes afterwards.
  const resultStart = getLocationOptionById((routeForm || formState).start);
  const resultEnd = getLocationOptionById((routeForm || formState).end);

  function updateForm(patch) {
    setValidationError("");
    setFormState((current) => ({ ...current, ...patch }));
  }

  async function findRoute() {
    setValidationError("");
    setError("");

    if (formState.start === formState.end) {
      setRoute(null);
      setValidationError("Pick two different places.");
      return;
    }

    requestRef.current?.abort();

    const submitted = formState;
    const cacheKey = JSON.stringify(submitted);
    const cachedRoute = cacheRef.current.get(cacheKey);

    if (cachedRoute) {
      startTransition(() => {
        setRoute(cachedRoute);
        setRouteForm(submitted);
      });
      return;
    }

    const controller = new AbortController();
    requestRef.current = controller;
    setLoading(true);

    try {
      const nextRoute = await requestRoute(submitted, { signal: controller.signal });
      cacheRef.current.set(cacheKey, nextRoute);

      startTransition(() => {
        setRoute(nextRoute);
        setRouteForm(submitted);
      });
    } catch (requestError) {
      if (requestError.name === "AbortError") {
        return;
      }

      startTransition(() => {
        setRoute(null);
        setError(requestError.message);
      });
    } finally {
      if (requestRef.current === controller) {
        requestRef.current = null;
        setLoading(false);
      }
    }
  }

  return (
    <main className="app-shell">
      <MapView startLocation={startLocation} endLocation={endLocation} route={route} loading={loading} />

      <aside className="sheet">
        <RouteForm
          startLocation={startLocation}
          endLocation={endLocation}
          locations={activeLocations}
          optimization={formState.optimization}
          loading={loading}
          validationError={validationError}
          onLocationSelect={(field, locationId) => updateForm({ [field]: locationId })}
          onOptimizationChange={(optimization) => updateForm({ optimization })}
          onSwap={() => updateForm({ start: formState.end, end: formState.start })}
          onSubmit={(event) => {
            event.preventDefault();
            findRoute();
          }}
        />

        <RouteResults
          route={route}
          error={error}
          loading={loading}
          startLocation={resultStart}
          endLocation={resultEnd}
          onRetry={findRoute}
        />
      </aside>
    </main>
  );
}

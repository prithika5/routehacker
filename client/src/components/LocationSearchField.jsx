import { useEffect, useId, useMemo, useState } from "react";

export default function LocationSearchField({ label, role, value, locations, onSelect }) {
  const [query, setQuery] = useState(value?.label || "");
  const [open, setOpen] = useState(false);
  const [activeIndex, setActiveIndex] = useState(0);
  const listId = useId();

  useEffect(() => {
    setQuery(value?.label || "");
  }, [value?.id, value?.label]);

  const filteredLocations = useMemo(() => {
    const normalizedQuery = query.trim().toLowerCase();
    const showAll = !normalizedQuery || normalizedQuery === value?.label?.trim().toLowerCase();

    if (showAll) {
      return locations;
    }

    return locations.filter((location) => `${location.label} ${location.area}`.toLowerCase().includes(normalizedQuery));
  }, [locations, query, value?.label]);

  useEffect(() => {
    setActiveIndex(0);
  }, [query, open]);

  function commitSelection(nextLocation) {
    if (!nextLocation) {
      setQuery(value?.label || "");
      setOpen(false);
      return;
    }

    onSelect(nextLocation.id);
    setQuery(nextLocation.label);
    setOpen(false);
  }

  function syncQueryToSelection() {
    const normalizedQuery = query.trim().toLowerCase();
    const currentLabel = value?.label?.trim().toLowerCase() || "";

    if (!normalizedQuery || normalizedQuery === currentLabel) {
      setQuery(value?.label || "");
      setOpen(false);
      return;
    }

    const exactMatch = filteredLocations.find((location) => location.label.trim().toLowerCase() === normalizedQuery);
    commitSelection(exactMatch || filteredLocations[0] || null);
  }

  function handleKeyDown(event) {
    if (event.key === "ArrowDown" || event.key === "ArrowUp") {
      event.preventDefault();
      if (!open) {
        setOpen(true);
        return;
      }
      const step = event.key === "ArrowDown" ? 1 : -1;
      const count = filteredLocations.length || 1;
      setActiveIndex((current) => (current + step + count) % count);
      return;
    }

    if (event.key === "Enter") {
      event.preventDefault();
      if (open && filteredLocations[activeIndex]) {
        commitSelection(filteredLocations[activeIndex]);
      } else {
        syncQueryToSelection();
      }
      return;
    }

    if (event.key === "Escape") {
      setQuery(value?.label || "");
      setOpen(false);
    }
  }

  const activeOptionId = open && filteredLocations[activeIndex] ? `${listId}-${filteredLocations[activeIndex].id}` : undefined;

  return (
    <div className={`place-field ${role}`}>
      <span className="place-pin" aria-hidden="true">
        <span>{role === "start" ? "A" : "B"}</span>
      </span>
      <label className="place-label">
        <span className="field-label">{label}</span>
        <input
          aria-label={label}
          role="combobox"
          aria-expanded={open}
          aria-controls={listId}
          aria-autocomplete="list"
          aria-activedescendant={activeOptionId}
          autoComplete="off"
          spellCheck="false"
          placeholder="Search campus places"
          value={query}
          onFocus={(event) => {
            event.target.select();
            setOpen(true);
          }}
          onChange={(event) => {
            setQuery(event.target.value);
            setOpen(true);
          }}
          onKeyDown={handleKeyDown}
          onBlur={() => {
            globalThis.setTimeout(() => {
              syncQueryToSelection();
            }, 120);
          }}
        />
      </label>

      {open ? (
        <div className="place-menu" id={listId} role="listbox" aria-label={`${label} suggestions`}>
          {filteredLocations.map((location, index) => (
            <div
              key={location.id}
              id={`${listId}-${location.id}`}
              role="option"
              aria-selected={index === activeIndex}
              className={`place-option${index === activeIndex ? " active" : ""}${value?.id === location.id ? " current" : ""}`}
              onMouseDown={(event) => event.preventDefault()}
              onMouseEnter={() => setActiveIndex(index)}
              onClick={() => commitSelection(location)}
            >
              <strong>{location.label}</strong>
              <span>{location.area}</span>
            </div>
          ))}
          {filteredLocations.length === 0 ? <p className="place-empty">No campus place matches “{query.trim()}”.</p> : null}
        </div>
      ) : null}
    </div>
  );
}

import React from "react";
import ReactDOM from "react-dom/client";
import "@fontsource/overpass/latin-400.css";
import "@fontsource/overpass/latin-600.css";
import "@fontsource/overpass/latin-800.css";
import "leaflet/dist/leaflet.css";
import App from "./App.jsx";
import "./styles.css";

ReactDOM.createRoot(document.getElementById("root")).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);

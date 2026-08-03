import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import App from "./App";
import "./index.css";

const rootEl = document.getElementById("root");
if (!rootEl) {
  throw new Error("WebDock: #root 挂载点缺失");
}

createRoot(rootEl).render(
  <StrictMode>
    <App />
  </StrictMode>,
);

import { defineConfig } from "vite";
import { fileURLToPath, URL } from "node:url";
import devtoolsJson from "vite-plugin-devtools-json";
import tailwindcss from "@tailwindcss/vite";
import { svelte } from "@sveltejs/vite-plugin-svelte";
import { router } from "sv-router/vite-plugin";
import compression from "vite-plugin-compression2";
import { execFileSync } from "node:child_process";

export default defineConfig(({ mode }) => {
  const isDev = mode === "development";
  const isDemo = isDev && process.env.ALIRO_UI_DEMO === "1";
  const backend = isDemo ? process.env.ALIRO_UI_BACKEND_URL : "http://localhost:8000";
  if (!backend) throw new Error("Demo backend URL is required; use bun run debug");
  let version = "source-snapshot";
  try { version = execFileSync("git", ["rev-parse", "--short=16", "HEAD"], { stdio: ["ignore", "pipe", "ignore"] }).toString().trim(); }
  catch { /* Source ZIPs do not include Git history. */ }

  return {
    plugins: [
      svelte(),
      router({
        path: "src/routes",
        allLazy: false,
        js: false,
      }),
      tailwindcss(),
      ...(isDev ? [devtoolsJson()] : []),
      compression({
        algorithms: [
          'gzip'
        ],
        deleteOriginalAssets: true
      })
    ],
    define: { __DEV__: isDev, __DEMO__: isDemo, __VERSION__: JSON.stringify(version) },
    resolve: {
      alias: {
        "@": fileURLToPath(new URL("./src", import.meta.url)),
        "$lib": fileURLToPath(new URL("./src/lib", import.meta.url)),
      },
    },
    build: {
      cssMinify: 'lightningcss',
      minify: 'oxc',
      target: 'es2020',
      rolldownOptions: {
        output: {
          codeSplitting: {
            minSize: 10000,
            groups: [
              {
                name: 'vendor',
                test: /node_modules/,
                priority: 2,
              },
              {
                name: 'components',
                test: /src\/lib\/components/,
                priority: 1,
              }
            ],
          },
          minify: true,
          polyfillRequire: false,
        }
      }
    },
    esbuild: {
      drop: isDev ? [] : ['console', 'debugger'],
      legalComments: 'none',
    },
    server: {
      proxy: Object.fromEntries([
        "/certificates", "/config", "/eth_get_config", "/nfc_get_presets",
        "/captive_portal_config",
        ...(isDemo ? ["/wifi_scan", "/reboot_device", "/reset_hk_pair",
          "/reset_wifi_cred", "/start_config_ap", "/ota"] : []),
        "/ws",
      ].map(path => [path, path === "/ws"
        ? { target: backend.replace(/^http/, "ws"), ws: true }
        : backend])),
    },
  };
});

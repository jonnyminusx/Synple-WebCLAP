import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'

// The plugin serves the whole UI as one HTML resource from WASM memory.
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  publicDir: false,
  base: './',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
  server: {
    port: 8080,
  },
})

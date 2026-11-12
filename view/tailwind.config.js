/**
 * Tailwind configuration.
 *
 * The classes live in the Solid components and in the colour tables of
 * src/plugin, so both the markup and the TypeScript sources are scanned.
 *
 * @type {import('tailwindcss').Config}
 */
export default {
  content: ['./index.html', './src/**/*.{ts,tsx}'],
  theme: {
    extend: {},
  },
  plugins: [],
};

/**
 * PostCSS configuration.
 *
 * Tailwind is processed as a PostCSS plugin, so the @tailwind directives of
 * src/index.css are expanded during the Vite build.
 */
export default {
  plugins: {
    tailwindcss: {},
    autoprefixer: {},
  },
};

/** @type {import('tailwindcss').Config} */
export default {
    content: [
        "./index.html",
        "./src/**/*.{js,ts,jsx,tsx}",
    ],
    theme: {
        extend: {
            colors: {
                'hyper-dark': '#0a0b0d',
                'hyper-gray': '#1e2026',
                'hyper-green': '#0ECB81',
                'hyper-red': '#F6465D',
            }
        },
    },
    plugins: [],
}

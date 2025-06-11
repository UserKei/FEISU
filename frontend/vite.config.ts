import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';

export default defineConfig({
	plugins: [sveltekit()],
	server: {
		host: 'localhost',
		port: 4173,
		open: true,
		proxy: {
		  '/api': {
			target: 'http://localhost:8080', 
			changeOrigin: true
		  }
		}
	  }
});

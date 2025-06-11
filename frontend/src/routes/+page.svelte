<script lang="ts">
    let message = "Loading...";
    let error = "";
    
    async function fetchData() {
      try {
        error = "";
        const res = await fetch("/api/hello");
        
        // 检查HTTP状态码
        if (!res.ok) {
          throw new Error(`HTTP error! status: ${res.status}`);
        }
        
        const data = await res.json();
        message = data.message;
      } catch (err) {
        console.error("Fetch error:", err);
        error = `Error: ${err.message || "Unknown error"}`;
        message = "Failed to load";
      }
    }
  </script>
  
  <button on:click={fetchData}>Get Message</button>
  {#if error}
    <p style="color: red;">{error}</p>
  {/if}
  <p>Backend says: {message}</p>
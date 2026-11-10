# C-Ink relay

Optional. Without it, your TICKEY syncs from C-Ink Settings whenever both are
on the same network. With it, the TICKEY can sync from any Wi-Fi it knows
(office, phone hotspot) while your computer is awake somewhere else.

The relay stores one blob per device. Each blob is sealed with that device's
AES-256-GCM key, which only C-Ink Settings and the device have, so the relay
holds ciphertext and nothing else. Blobs expire after 48 hours.

## Deploy (Cloudflare Workers free tier)

```sh
cd relay
npx wrangler login
npx wrangler kv namespace create BLOBS     # paste the id into wrangler.toml
npx wrangler secret put WRITE_TOKEN        # any long random string
npx wrangler deploy
```

Then in C-Ink Settings → Away mode, enter the worker URL and the same
WRITE_TOKEN, and press **Update settings only** on the Device page with the
TICKEY plugged in so it learns the URL.

Any host works if you prefer something else: the contract is two endpoints,
documented at the top of `src/index.js`.

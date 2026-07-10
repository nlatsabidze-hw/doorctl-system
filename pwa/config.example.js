// ═══════════════════════════════════════════════════════════
// DoorCTL - PWA Configuration Template
// ═══════════════════════════════════════════════════════════
// This file is PUBLIC (safe to upload to GitHub).
// Real credentials must NEVER be written here.
//
// Usage:
//   1. Copy this file and rename it to "config.js"
//   2. Replace all YOUR_..._HERE placeholders with real values
//   3. config.js must NEVER be uploaded to GitHub
//      (add it to .gitignore)
//   4. index.html loads config.js via <script src="config.js">
//      before the main app script
// ═══════════════════════════════════════════════════════════

const SUPABASE_URL = 'https://YOUR_PROJECT_REF.supabase.co';
const SUPABASE_KEY = 'YOUR_SUPABASE_PUBLISHABLE_KEY_HERE';

// One Blynk auth token per door (each door is its own Blynk device/template)
const BLYNK_TOKENS = {
  'W1': 'YOUR_BLYNK_TOKEN_DOOR_1',
  'W2': 'YOUR_BLYNK_TOKEN_DOOR_2',
  'W3': 'YOUR_BLYNK_TOKEN_DOOR_3',
  'W4': 'YOUR_BLYNK_TOKEN_DOOR_4'
};

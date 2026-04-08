/* ═══════════════════════════════════════════════════════════════
   VOID RUNNER — Enhanced Edition
   game.js — Complete game logic
   ═══════════════════════════════════════════════════════════════ */
(function () {
  "use strict";

  /* ══ Canvas ═══════════════════════════════════════════════════ */
  const canvas = document.getElementById("game-canvas");
  const ctx    = canvas.getContext("2d");
  let W = 0, H = 0;
  function resize() { W = canvas.width = window.innerWidth; H = canvas.height = window.innerHeight; }
  window.addEventListener("resize", resize);
  resize();

  /* ══ Colour palette ═══════════════════════════════════════════ */
  const C = {
    cyan:    "#00f5ff", magenta: "#ff00cc", yellow: "#ffe600",
    green:   "#00ff88", orange:  "#ff6600", red:    "#ff2244",
    white:   "#ffffff", purple:  "#aa00ff",
  };

  /* ══ Screen manager ═══════════════════════════════════════════ */
  const screens = {
    menu:     document.getElementById("screen-menu"),
    how:      document.getElementById("screen-how"),
    shop:     document.getElementById("screen-shop"),
    pause:    document.getElementById("screen-pause"),
    hud:      document.getElementById("screen-hud"),
    wave:     document.getElementById("screen-wave"),
    boss:     document.getElementById("screen-boss"),
    gameover: document.getElementById("screen-gameover"),
    win:      document.getElementById("screen-win"),
  };
  function showScreen(...ids) {
    Object.values(screens).forEach(s => s.classList.remove("active"));
    ids.forEach(id => screens[id] && screens[id].classList.add("active"));
  }

  /* ══ DOM refs ═════════════════════════════════════════════════ */
  const hudScore      = document.getElementById("hud-score");
  const hudWave       = document.getElementById("hud-wave");
  const hudCredits    = document.getElementById("hud-credits");
  const hudCombo      = document.getElementById("hud-combo");
  const barShield     = document.getElementById("bar-shield");
  const barHull       = document.getElementById("bar-hull");
  const barSpecial    = document.getElementById("bar-special");
  const hudWeapSlots  = document.getElementById("hud-weapon-slots");
  const specialName   = document.getElementById("special-name");
  const missileCount  = document.getElementById("missile-count");
  const goScore       = document.getElementById("go-score");
  const goBest        = document.getElementById("go-best");
  const goStats       = document.getElementById("go-stats");
  const winScore      = document.getElementById("win-score");
  const winBest       = document.getElementById("win-best");
  const winSub        = document.getElementById("win-sub");
  const waveText      = document.getElementById("wave-text");
  const waveSub       = document.getElementById("wave-sub");
  const waveSector    = document.getElementById("wave-sector-text");
  const waveObjective = document.getElementById("wave-objective");
  const bossTitle     = document.getElementById("boss-title");
  const bossSub       = document.getElementById("boss-sub");
  const shopGrid      = document.getElementById("shop-grid");
  const shopCreditsV  = document.getElementById("shop-credits-val");
  const shopWaveInfo  = document.getElementById("shop-wave-info");
  const shopObjectives= document.getElementById("shop-objectives");
  const bestDisp      = document.getElementById("best-score-display");
  const floatTexts    = document.getElementById("float-texts");

  /* ══ Button wiring ════════════════════════════════════════════ */
  document.getElementById("btn-start").addEventListener("click", startGame);
  document.getElementById("btn-how").addEventListener("click", () => showScreen("how"));
  document.getElementById("btn-back-how").addEventListener("click", () => showScreen("menu"));
  document.getElementById("btn-upgrades-menu").addEventListener("click", () => { openShopMenu(); });
  document.getElementById("btn-shop-continue").addEventListener("click", shopContinue);
  document.getElementById("btn-resume").addEventListener("click", resumeGame);
  document.getElementById("btn-pause-menu").addEventListener("click", goMenu);
  document.getElementById("btn-restart").addEventListener("click", startGame);
  document.getElementById("btn-go-menu").addEventListener("click", goMenu);
  document.getElementById("btn-win-restart").addEventListener("click", startGame);
  document.getElementById("btn-win-menu").addEventListener("click", goMenu);

  function goMenu() {
    gameRunning = false; gamePaused = false;
    bestDisp.textContent = "BEST SCORE: " + bestScore;
    showScreen("menu");
  }

  /* ══ Keyboard ═════════════════════════════════════════════════ */
  const keys = {};
  const justPressed = {};
  document.addEventListener("keydown", e => {
    if (keys[e.code]) return;
    keys[e.code] = true;
    justPressed[e.code] = true;
    if (["Space","ArrowUp","ArrowDown","ArrowLeft","ArrowRight"].includes(e.code)) e.preventDefault();
    if (e.code === "Tab") { e.preventDefault(); togglePause(); }
    if (gameRunning && !gamePaused) {
      if (e.code === "KeyE")   activateSpecial();
      if (e.code === "KeyQ")   shieldBurst();
      if (e.code === "KeyR")   launchMissile();
      if (e.code === "Digit1") setWeapon(0);
      if (e.code === "Digit2") setWeapon(1);
      if (e.code === "Digit3") setWeapon(2);
    }
  });
  document.addEventListener("keyup", e => { keys[e.code] = false; });

  /* ══ Constants ════════════════════════════════════════════════ */
  const TOTAL_SECTORS   = 3;
  const WAVES_PER_SECTOR= 5;
  const SHIP_DRAG       = 0.984;
  const SHIP_THRUST_BASE= 0.20;
  const SHIP_ROT_BASE   = 0.056;
  const BULLET_LIFE     = 58;
  const SHIELD_MAX      = 100;
  const HULL_MAX        = 100;
  const SPECIAL_MAX     = 100;
  const FIRE_CD_BASE    = 18;
  const COMBO_DECAY     = 280;

  /* ══ Global game state ════════════════════════════════════════ */
  let gameRunning    = false;
  let gamePaused     = false;
  let bestScore      = 0;

  let score          = 0;
  let credits        = 0;
  let sector         = 1;
  let waveInSector   = 0;
  let waveGlobal     = 0;
  let waveActive     = false;
  let waveAnnouncing = false;
  let frameCount     = 0;

  let combo          = 1;
  let comboTimer     = 0;
  let totalKills     = 0;
  let asteroidsDestroyed = 0;
  let dronesDestroyed    = 0;
  let bossesDefeated     = 0;
  let waveObjectives     = {};
  let objProgress        = {};

  // Weapon system
  const WEAPONS = [
    { name: "CANNON",  key:"1", fireRate: FIRE_CD_BASE,      spread:0,    bulletCount:1, bulletCol: C.cyan,    bulletSpd:14 },
    { name: "SCATTER", key:"2", fireRate: FIRE_CD_BASE * 1.4, spread:0.22, bulletCount:3, bulletCol: C.yellow,  bulletSpd:13 },
    { name: "PLASMA",  key:"3", fireRate: FIRE_CD_BASE * 2.2, spread:0,    bulletCount:1, bulletCol: C.magenta, bulletSpd:18 },
  ];
  let currentWeapon = 0;
  let weaponsUnlocked = [true, false, false];
  let fireCooldown = 0;

  // Abilities
  const SPECIALS = ["NOVA BURST", "TIME WARP", "EMP PULSE"];
  let currentSpecial = 0;
  let specialCharge  = 0;
  let specialCooldown= 0;
  let missiles       = 0;
  let shieldBurstCD  = 0;
  let timeWarpActive = false;
  let timeWarpTimer  = 0;
  let empActive      = false;
  let empTimer       = 0;

  // Ship stats (base + upgrades)
  const upgrades = {
    thrustPower:   { level:0, max:4, cost:[150,280,450,650], desc:"Increases engine thrust and top speed." },
    fireRate:      { level:0, max:4, cost:[120,240,400,600], desc:"Reduces weapon cooldown." },
    shieldRegen:   { level:0, max:3, cost:[200,380,600],     desc:"Shield slowly regenerates over time." },
    hullPlating:   { level:0, max:3, cost:[180,350,550],     desc:"Increases hull integrity." },
    bulletDamage:  { level:0, max:4, cost:[140,260,420,640], desc:"Increases bullet damage." },
    missileSystem: { level:0, max:3, cost:[300,500,750],     desc:"Unlocks and upgrades missile payload." },
    specialCharge: { level:0, max:3, cost:[220,420,680],     desc:"Special ability charges faster." },
    weaponScatter: { level:0, max:1, cost:[350],             desc:"Unlocks SCATTER weapon mode." },
    weaponPlasma:  { level:0, max:1, cost:[500],             desc:"Unlocks PLASMA weapon mode." },
  };

  let ship;
  let bullets        = [];
  let enemyBullets   = [];
  let asteroids      = [];
  let drones         = [];
  let carriers       = [];
  let mines          = [];
  let boss           = null;
  let particles      = [];
  let powerups       = [];
  let stars          = [];
  let nebulae        = [];
  let shockwaves     = [];
  let missileObjects = [];

  /* ══ Derived stats ════════════════════════════════════════════ */
  function getThrust()   { return SHIP_THRUST_BASE + upgrades.thrustPower.level * 0.04; }
  function getRotSpeed() { return SHIP_ROT_BASE; }
  function getFireRate() { return Math.max(6, FIRE_CD_BASE - upgrades.fireRate.level * 3); }
  function getBulletDmg(){ return 1 + upgrades.bulletDamage.level * 0.5; }
  function getMaxHull()  { return HULL_MAX + upgrades.hullPlating.level * 30; }
  function getSpecialChargeRate() { return 0.12 + upgrades.specialCharge.level * 0.06; }
  function getMissileMax(){ return upgrades.missileSystem.level * 3; }

  /* ══ Float text ═══════════════════════════════════════════════ */
  function spawnFloatText(x, y, text, color = "#fff", size = 13) {
    const el = document.createElement("div");
    el.className = "float-text";
    el.textContent = text;
    el.style.left  = x + "px";
    el.style.top   = y + "px";
    el.style.color = color;
    el.style.fontSize = size + "px";
    el.style.textShadow = `0 0 8px ${color}`;
    floatTexts.appendChild(el);
    setTimeout(() => el.remove(), 1200);
  }

  /* ══ Stars & nebula ═══════════════════════════════════════════ */
  function buildStars() {
    stars = []; nebulae = [];
    for (let i = 0; i < 220; i++) {
      stars.push({
        x: Math.random() * W, y: Math.random() * H,
        r: Math.random() * 1.5 + 0.2,
        b: Math.random(), db: (Math.random() - 0.5) * 0.01,
        speed: Math.random() * 0.35 + 0.05,
        layer: Math.floor(Math.random() * 3),
      });
    }
    const nebCols = [C.cyan, C.magenta, "#0044ff", "#550077", "#003355"];
    for (let i = 0; i < 6; i++) {
      nebulae.push({
        x: Math.random() * W, y: Math.random() * H,
        r: Math.random() * 260 + 80,
        col: nebCols[i % nebCols.length],
        a: Math.random() * 0.055 + 0.018,
      });
    }
  }

  function drawBackground() {
    // Nebulae
    nebulae.forEach(n => {
      const g = ctx.createRadialGradient(n.x, n.y, 0, n.x, n.y, n.r);
      const hex = Math.round(n.a * 255).toString(16).padStart(2, "0");
      g.addColorStop(0, n.col + hex);
      g.addColorStop(1, "transparent");
      ctx.fillStyle = g; ctx.beginPath();
      ctx.arc(n.x, n.y, n.r, 0, Math.PI * 2); ctx.fill();
    });
    // Stars
    const speedMult = timeWarpActive ? 0.2 : 1;
    stars.forEach(s => {
      s.b += s.db;
      if (s.b > 1 || s.b < 0.15) s.db *= -1;
      if (gameRunning && !gamePaused) {
        const sp = [0.06, 0.18, 0.38][s.layer] * speedMult;
        s.y += sp;
        if (s.y > H) { s.y = 0; s.x = Math.random() * W; }
      }
      ctx.globalAlpha = s.b;
      ctx.fillStyle = "#fff";
      ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2); ctx.fill();
    });
    ctx.globalAlpha = 1;
  }

  /* ══ Ship ═════════════════════════════════════════════════════ */
  function createShip() {
    return {
      x: W / 2, y: H / 2,
      vx: 0, vy: 0,
      angle: -Math.PI / 2,
      shield: SHIELD_MAX,
      hull: getMaxHull(),
      invincible: 0,
      dead: false,
      thrustParticleTimer: 0,
      shieldRegenTimer: 0,
    };
  }

  function drawShip() {
    if (ship.dead) return;
    const s = ship;
    if (s.invincible > 0 && Math.floor(s.invincible / 5) % 2 === 0) return;

    ctx.save();
    ctx.translate(s.x, s.y);
    ctx.rotate(s.angle + Math.PI / 2);

    const isThrusting = (keys["KeyW"] || keys["ArrowUp"]) && gameRunning && !gamePaused;
    const glowCol = timeWarpActive ? C.purple : (empActive ? C.yellow : C.cyan);

    // Engine flame
    if (isThrusting) {
      const fl = 14 + Math.random() * 12;
      const fg = ctx.createRadialGradient(0, 18, 0, 0, 18, fl);
      fg.addColorStop(0, glowCol + "cc");
      fg.addColorStop(0.5, C.magenta + "55");
      fg.addColorStop(1, "transparent");
      ctx.fillStyle = fg;
      ctx.beginPath(); ctx.arc(0, 18, fl, 0, Math.PI * 2); ctx.fill();
    }

    ctx.shadowBlur = 22; ctx.shadowColor = glowCol;
    ctx.strokeStyle = glowCol; ctx.lineWidth = 2;

    // Main body
    ctx.beginPath();
    ctx.moveTo(0, -22);
    ctx.lineTo(-13, 14); ctx.lineTo(-6, 8);
    ctx.lineTo(0, 13);   ctx.lineTo(6, 8);
    ctx.lineTo(13, 14);
    ctx.closePath(); ctx.stroke();
    ctx.fillStyle = glowCol + "14"; ctx.fill();

    // Wing accents
    ctx.beginPath();
    ctx.moveTo(-13, 14); ctx.lineTo(-18, 6); ctx.lineTo(-10, 0);
    ctx.strokeStyle = glowCol + "88"; ctx.lineWidth = 1;
    ctx.stroke();
    ctx.beginPath();
    ctx.moveTo(13, 14); ctx.lineTo(18, 6); ctx.lineTo(10, 0);
    ctx.stroke();

    // Core
    ctx.shadowBlur = 12;
    ctx.fillStyle = glowCol;
    ctx.beginPath(); ctx.arc(0, -3, 3.5, 0, Math.PI * 2); ctx.fill();

    ctx.restore();

    // Shield ring
    if (s.shield > 0 && s.invincible > 50) {
      const a = Math.min(1, (s.invincible - 50) / 30) * 0.7;
      ctx.save(); ctx.globalAlpha = a;
      ctx.beginPath(); ctx.arc(s.x, s.y, 28, 0, Math.PI * 2);
      ctx.strokeStyle = C.cyan; ctx.lineWidth = 1.5;
      ctx.shadowBlur = 10; ctx.shadowColor = C.cyan;
      ctx.stroke(); ctx.restore();
    }
  }

  function updateShip() {
    if (ship.dead) return;
    const s = ship;
    const tw = timeWarpActive ? 0.45 : 1;

    if (keys["KeyA"] || keys["ArrowLeft"])  s.angle -= getRotSpeed() * tw;
    if (keys["KeyD"] || keys["ArrowRight"]) s.angle += getRotSpeed() * tw;
    if (keys["KeyW"] || keys["ArrowUp"]) {
      s.vx += Math.cos(s.angle) * getThrust() * tw;
      s.vy += Math.sin(s.angle) * getThrust() * tw;
      // Thrust particles
      s.thrustParticleTimer++;
      if (s.thrustParticleTimer % 3 === 0) {
        spawnParticles(
          s.x - Math.cos(s.angle) * 18,
          s.y - Math.sin(s.angle) * 18,
          C.cyan, 2, 1.5, true
        );
      }
    } else { s.thrustParticleTimer = 0; }
    if (keys["KeyS"] || keys["ArrowDown"]) { s.vx *= 0.92; s.vy *= 0.92; }

    const spd = Math.hypot(s.vx, s.vy);
    const maxSpd = 8 + upgrades.thrustPower.level * 0.8;
    if (spd > maxSpd) { s.vx = s.vx / spd * maxSpd; s.vy = s.vy / spd * maxSpd; }
    s.vx *= SHIP_DRAG; s.vy *= SHIP_DRAG;
    s.x += s.vx * tw; s.y += s.vy * tw;

    if (s.x < -30) s.x = W + 30;
    if (s.x > W+30) s.x = -30;
    if (s.y < -30) s.y = H + 30;
    if (s.y > H+30) s.y = -30;

    if (s.invincible > 0) s.invincible--;

    // Shield regen
    if (upgrades.shieldRegen.level > 0 && s.shield < SHIELD_MAX) {
      s.shieldRegenTimer++;
      const regenRate = 180 - upgrades.shieldRegen.level * 40;
      if (s.shieldRegenTimer >= regenRate) {
        s.shieldRegenTimer = 0;
        s.shield = Math.min(SHIELD_MAX, s.shield + 2);
        updateBars();
      }
    }

    // Special charge
    if (specialCharge < SPECIAL_MAX) {
      specialCharge = Math.min(SPECIAL_MAX, specialCharge + getSpecialChargeRate());
    }
    if (specialCooldown > 0) specialCooldown--;
    if (shieldBurstCD > 0) shieldBurstCD--;
    if (timeWarpActive) { timeWarpTimer--; if (timeWarpTimer <= 0) { timeWarpActive = false; } }
    if (empActive)      { empTimer--;      if (empTimer <= 0)      { empActive = false; } }
    updateBars();
  }

  /* ══ Weapons & Fire ═══════════════════════════════════════════ */
  function setWeapon(idx) {
    if (!weaponsUnlocked[idx]) {
      spawnFloatText(W/2 - 80, H/2, "WEAPON LOCKED", C.red); return;
    }
    currentWeapon = idx;
    renderWeaponSlots();
  }

  function fireBullet() {
    if (!gameRunning || gamePaused || ship.dead) return;
    if (fireCooldown > 0) return;
    const w = WEAPONS[currentWeapon];
    fireCooldown = Math.round(getFireRate() * (currentWeapon === 0 ? 1 : currentWeapon === 1 ? 1.3 : 2));

    const angles = [ship.angle];
    if (w.bulletCount === 3) {
      angles.push(ship.angle - w.spread, ship.angle + w.spread);
    }

    angles.forEach(a => {
      const isPlasma = currentWeapon === 2;
      bullets.push({
        x: ship.x + Math.cos(ship.angle) * 24,
        y: ship.y + Math.sin(ship.angle) * 24,
        vx: Math.cos(a) * w.bulletSpd + ship.vx * 0.4,
        vy: Math.sin(a) * w.bulletSpd + ship.vy * 0.4,
        life: BULLET_LIFE + (isPlasma ? 12 : 0),
        col: w.bulletCol,
        dmg: getBulletDmg() * (isPlasma ? 2.5 : 1),
        r: isPlasma ? 6 : 3.5,
        plasma: isPlasma,
      });
    });
  }

  function launchMissile() {
    if (!gameRunning || gamePaused || ship.dead) return;
    if (missiles <= 0) { spawnFloatText(ship.x, ship.y - 30, "NO MISSILES", C.red); return; }
    missiles--;
    updateHUD();

    // Find nearest target
    let target = null, bestDist = Infinity;
    [...asteroids, ...drones, ...carriers, ...mines, ...(boss ? [boss] : [])].forEach(e => {
      const d = Math.hypot(e.x - ship.x, e.y - ship.y);
      if (d < bestDist) { bestDist = d; target = e; }
    });

    missileObjects.push({
      x: ship.x, y: ship.y,
      vx: Math.cos(ship.angle) * 6, vy: Math.sin(ship.angle) * 6,
      target, life: 180, trail: [],
    });
  }

  function updateBullets() {
    const tw = timeWarpActive ? 0.45 : 1;
    for (let i = bullets.length - 1; i >= 0; i--) {
      const b = bullets[i];
      b.x += b.vx * tw; b.y += b.vy * tw; b.life--;
      if (b.life <= 0 || outOfBounds(b)) bullets.splice(i, 1);
    }
    for (let i = enemyBullets.length - 1; i >= 0; i--) {
      const b = enemyBullets[i];
      b.x += b.vx * tw; b.y += b.vy * tw; b.life--;
      if (b.life <= 0 || outOfBounds(b)) enemyBullets.splice(i, 1);
    }
    // Missiles
    for (let i = missileObjects.length - 1; i >= 0; i--) {
      const m = missileObjects[i];
      m.trail.push({ x: m.x, y: m.y });
      if (m.trail.length > 20) m.trail.shift();

      // Homing
      if (m.target && !m.target.dead && isEntityAlive(m.target)) {
        const dx = m.target.x - m.x, dy = m.target.y - m.y;
        const dist = Math.hypot(dx, dy);
        const desiredAngle = Math.atan2(dy, dx);
        const currentAngle = Math.atan2(m.vy, m.vx);
        let diff = desiredAngle - currentAngle;
        while (diff > Math.PI) diff -= Math.PI * 2;
        while (diff < -Math.PI) diff += Math.PI * 2;
        const newAngle = currentAngle + diff * 0.08;
        const spd = Math.min(12, Math.hypot(m.vx, m.vy) + 0.3);
        m.vx = Math.cos(newAngle) * spd;
        m.vy = Math.sin(newAngle) * spd;
        if (dist < 20) { missileHit(m, i); continue; }
      } else {
        // Keep going straight
        const spd = Math.min(14, Math.hypot(m.vx, m.vy) + 0.4);
        const ang = Math.atan2(m.vy, m.vx);
        m.vx = Math.cos(ang) * spd; m.vy = Math.sin(ang) * spd;
      }

      m.x += m.vx * tw; m.y += m.vy * tw;
      m.life--;
      if (m.life <= 0) { spawnParticles(m.x, m.y, C.orange, 12, 4); missileObjects.splice(i, 1); }
    }
  }

  function isEntityAlive(e) {
    return asteroids.includes(e) || drones.includes(e) || carriers.includes(e) || mines.includes(e) || boss === e;
  }

  function missileHit(m, idx) {
    spawnParticles(m.x, m.y, C.orange, 30, 6);
    spawnShockwave(m.x, m.y, 80, C.orange);
    // Area damage
    const area = 80;
    [...asteroids, ...drones, ...carriers, ...mines].forEach(e => {
      if (Math.hypot(e.x - m.x, e.y - m.y) < area) damageEnemy(e, 5);
    });
    if (boss && Math.hypot(boss.x - m.x, boss.y - m.y) < area) damageBoss(5);
    missileObjects.splice(idx, 1);
  }

  function drawBullets() {
    [...bullets, ...enemyBullets].forEach(b => {
      ctx.save();
      ctx.shadowBlur = b.plasma ? 24 : 14;
      ctx.shadowColor = b.col;
      ctx.fillStyle = b.col;
      ctx.beginPath(); ctx.arc(b.x, b.y, b.r || 3.5, 0, Math.PI * 2); ctx.fill();
      // Trail
      ctx.globalAlpha = 0.25;
      ctx.beginPath(); ctx.arc(b.x - (b.vx||0) * 0.6, b.y - (b.vy||0) * 0.6, (b.r||3.5) * 0.7, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    });
    // Missiles
    missileObjects.forEach(m => {
      ctx.save();
      // Trail
      m.trail.forEach((t, i) => {
        ctx.globalAlpha = (i / m.trail.length) * 0.5;
        ctx.fillStyle = C.orange;
        ctx.beginPath(); ctx.arc(t.x, t.y, 2, 0, Math.PI * 2); ctx.fill();
      });
      ctx.globalAlpha = 1;
      ctx.shadowBlur = 14; ctx.shadowColor = C.orange;
      ctx.fillStyle = C.orange;
      ctx.beginPath(); ctx.arc(m.x, m.y, 5, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    });
  }

  /* ══ Abilities ════════════════════════════════════════════════ */
  function activateSpecial() {
    if (specialCharge < SPECIAL_MAX || specialCooldown > 0) {
      spawnFloatText(ship.x, ship.y - 40, "NOT READY", C.red); return;
    }
    specialCharge = 0; specialCooldown = 240;
    const specials = ["NOVA BURST", "TIME WARP", "EMP PULSE"];
    const which = currentSpecial % specials.length;

    if (which === 0) { // Nova burst — big explosion around ship
      spawnShockwave(ship.x, ship.y, 180, C.cyan);
      spawnParticles(ship.x, ship.y, C.cyan, 50, 8);
      const range = 180;
      for (let ai = asteroids.length - 1; ai >= 0; ai--) {
        if (Math.hypot(asteroids[ai].x - ship.x, asteroids[ai].y - ship.y) < range)
          damageEnemy(asteroids[ai], 999);
      }
      for (let di = drones.length - 1; di >= 0; di--) {
        if (Math.hypot(drones[di].x - ship.x, drones[di].y - ship.y) < range)
          damageEnemy(drones[di], 999);
      }
      for (let ci = carriers.length - 1; ci >= 0; ci--) {
        if (Math.hypot(carriers[ci].x - ship.x, carriers[ci].y - ship.y) < range)
          damageEnemy(carriers[ci], 4);
      }
      if (boss && Math.hypot(boss.x - ship.x, boss.y - ship.y) < range) damageBoss(8);
      spawnFloatText(ship.x, ship.y - 50, "NOVA BURST!", C.cyan, 18);
    } else if (which === 1) { // Time warp
      timeWarpActive = true; timeWarpTimer = 300;
      spawnFloatText(ship.x, ship.y - 50, "TIME WARP!", C.purple, 18);
    } else { // EMP pulse — stuns enemies
      empActive = true; empTimer = 200;
      spawnShockwave(ship.x, ship.y, 300, C.yellow);
      spawnFloatText(ship.x, ship.y - 50, "EMP PULSE!", C.yellow, 18);
      // Clear enemy bullets
      enemyBullets = [];
    }
  }

  function shieldBurst() {
    if (shieldBurstCD > 0 || ship.shield < 20) {
      spawnFloatText(ship.x, ship.y - 40, "SHIELD LOW", C.red); return;
    }
    ship.shield = Math.max(0, ship.shield - 20);
    ship.invincible = 120;
    shieldBurstCD = 360;
    spawnShockwave(ship.x, ship.y, 60, C.cyan);
    spawnParticles(ship.x, ship.y, C.cyan, 20, 4);
    spawnFloatText(ship.x, ship.y - 40, "SHIELD BURST", C.cyan);
    updateBars();
  }

  /* ══ Shockwaves ═══════════════════════════════════════════════ */
  function spawnShockwave(x, y, maxR, col) {
    shockwaves.push({ x, y, r: 5, maxR, col, life: 1 });
  }
  function updateShockwaves() {
    for (let i = shockwaves.length - 1; i >= 0; i--) {
      const s = shockwaves[i];
      s.r += (s.maxR - s.r) * 0.12;
      s.life -= 0.04;
      if (s.life <= 0) shockwaves.splice(i, 1);
    }
  }
  function drawShockwaves() {
    shockwaves.forEach(s => {
      ctx.save();
      ctx.globalAlpha = s.life * 0.7;
      ctx.strokeStyle = s.col;
      ctx.lineWidth = 2;
      ctx.shadowBlur = 20; ctx.shadowColor = s.col;
      ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, Math.PI * 2); ctx.stroke();
      ctx.restore();
    });
  }

  /* ══ Asteroids ════════════════════════════════════════════════ */
  const AST_TIERS = [
    { r: 44, hp: 3, baseScore: 20,  credits: 8,  splits: 2 },
    { r: 25, hp: 2, baseScore: 40,  credits: 5,  splits: 2 },
    { r: 13, hp: 1, baseScore: 80,  credits: 3,  splits: 0 },
  ];

  function spawnAsteroid(tier = 0, x, y, vx, vy) {
    const t   = AST_TIERS[tier];
    const pos = (x !== undefined) ? { x, y } : spawnEdge(t.r + 10);
    const ang = Math.atan2(H/2 - pos.y, W/2 - pos.x) + (Math.random() - 0.5) * 1.3;
    const spd = Math.random() * 1.6 + 0.4 + waveGlobal * 0.07;
    const pts = [];
    const pc  = 9 + Math.floor(Math.random() * 4);
    for (let i = 0; i < pc; i++) {
      const a = (i / pc) * Math.PI * 2;
      pts.push({ x: Math.cos(a) * t.r * (0.7 + Math.random() * 0.4), y: Math.sin(a) * t.r * (0.7 + Math.random() * 0.4) });
    }
    return {
      x: pos.x, y: pos.y,
      vx: vx !== undefined ? vx : Math.cos(ang) * spd,
      vy: vy !== undefined ? vy : Math.sin(ang) * spd,
      rot: 0, rotSpd: (Math.random() - 0.5) * 0.03,
      tier, r: t.r, hp: t.hp, maxHp: t.hp,
      score: t.baseScore, credits: t.credits,
      splits: t.splits, pts, flash: 0,
      type: "asteroid",
    };
  }

  function updateAsteroids() {
    const tw = (timeWarpActive || empActive) ? 0.2 : 1;
    asteroids.forEach(a => {
      a.x += a.vx * tw; a.y += a.vy * tw;
      a.rot += a.rotSpd * tw;
      if (a.flash > 0) a.flash--;
      wrapEntity(a);
    });
  }

  function drawAsteroids() {
    asteroids.forEach(a => {
      ctx.save();
      ctx.translate(a.x, a.y); ctx.rotate(a.rot);
      const col = a.flash > 0 ? "#fff" : C.cyan;
      ctx.shadowBlur = a.flash > 0 ? 30 : 8;
      ctx.shadowColor = col; ctx.strokeStyle = col; ctx.lineWidth = 1.8;
      ctx.beginPath();
      a.pts.forEach((p, i) => i === 0 ? ctx.moveTo(p.x, p.y) : ctx.lineTo(p.x, p.y));
      ctx.closePath(); ctx.stroke();
      if (a.maxHp > 1) drawHpBar(0, a.r + 9, a.r * 1.4, a.hp, a.maxHp, C.cyan);
      ctx.restore();
    });
  }

  /* ══ Drones ═══════════════════════════════════════════════════ */
  function spawnDrone(tier = 0) {
    const pos = spawnEdge(30);
    const types = [
      { r:13, speed:1.8, hp:2, score:120, credits:15, fireRate:180, col: C.magenta },
      { r:17, speed:1.3, hp:4, score:220, credits:28, fireRate:120, col: C.yellow  },
      { r:11, speed:2.6, hp:1, score:80,  credits:10, fireRate:999, col: C.green   }, // fast kamikaze
    ];
    const t = types[Math.min(tier, types.length - 1)];
    return {
      x: pos.x, y: pos.y, vx: 0, vy: 0,
      hp: t.hp, maxHp: t.hp,
      r: t.r, speed: t.speed, col: t.col,
      score: t.score, credits: t.credits,
      fireRate: t.fireRate, fireTimer: Math.random() * t.fireRate | 0,
      angle: 0, tier, flash: 0, type: "drone",
    };
  }

  function updateDrones() {
    const tw = (timeWarpActive || empActive) ? 0.2 : 1;
    drones.forEach(d => {
      if (d.flash > 0) d.flash--;
      const dx = ship.x - d.x, dy = ship.y - d.y;
      const dist = Math.hypot(dx, dy);
      d.angle = Math.atan2(dy, dx);
      const maxSpd = d.speed + waveGlobal * 0.05;

      if (d.tier === 2) { // Kamikaze — charge straight
        d.vx += (dx/dist) * d.speed * 0.12;
        d.vy += (dy/dist) * d.speed * 0.12;
      } else if (dist > 180) {
        d.vx += (dx/dist) * d.speed * 0.08;
        d.vy += (dy/dist) * d.speed * 0.08;
      } else {
        const perp = d.angle + Math.PI / 2;
        d.vx += Math.cos(perp) * d.speed * 0.04;
        d.vy += Math.sin(perp) * d.speed * 0.04;
      }
      const spd = Math.hypot(d.vx, d.vy);
      if (spd > maxSpd) { d.vx = d.vx/spd * maxSpd; d.vy = d.vy/spd * maxSpd; }

      d.x += d.vx * tw; d.y += d.vy * tw;
      wrapEntity(d);

      if (!empActive) {
        d.fireTimer--;
        if (d.fireTimer <= 0) {
          d.fireTimer = Math.max(50, d.fireRate - waveGlobal * 3);
          if (d.tier !== 2) fireEnemyBullet(d.x, d.y, d.angle, d.col);
        }
      }
    });
  }

  function drawDrones() {
    drones.forEach(d => {
      ctx.save();
      ctx.translate(d.x, d.y);
      const col = (d.flash > 0 || empActive) ? (empActive ? C.yellow : "#fff") : d.col;
      ctx.rotate(d.angle + Math.PI / 2);
      ctx.shadowBlur = 18; ctx.shadowColor = col;
      ctx.strokeStyle = col; ctx.lineWidth = 1.8;
      const sides = d.tier === 2 ? 3 : 6;
      ctx.beginPath();
      for (let i = 0; i < sides; i++) {
        const a = (i / sides) * Math.PI * 2;
        i === 0 ? ctx.moveTo(Math.cos(a)*d.r, Math.sin(a)*d.r) : ctx.lineTo(Math.cos(a)*d.r, Math.sin(a)*d.r);
      }
      ctx.closePath(); ctx.stroke();
      ctx.fillStyle = col + "22"; ctx.fill();
      ctx.fillStyle = col; ctx.shadowBlur = 8;
      ctx.beginPath(); ctx.arc(0, 0, 3.5, 0, Math.PI * 2); ctx.fill();
      if (d.maxHp > 1) drawHpBar(0, d.r + 5, d.r * 2, d.hp, d.maxHp, col);
      ctx.restore();
    });
  }

  /* ══ Carriers ═════════════════════════════════════════════════ */
  function spawnCarrier() {
    const pos = spawnEdge(60);
    return {
      x: pos.x, y: pos.y, vx: 0, vy: 0,
      r: 38, hp: 12, maxHp: 12,
      score: 500, credits: 60,
      speed: 0.7, angle: 0, rot: 0,
      spawnTimer: 200, spawnRate: 200,
      flash: 0, type: "carrier",
    };
  }

  function updateCarriers() {
    const tw = (timeWarpActive || empActive) ? 0.2 : 1;
    carriers.forEach(c => {
      if (c.flash > 0) c.flash--;
      const dx = ship.x - c.x, dy = ship.y - c.y;
      const dist = Math.hypot(dx, dy);
      if (dist > 250) {
        c.vx += (dx/dist) * c.speed * 0.04;
        c.vy += (dy/dist) * c.speed * 0.04;
      }
      const spd = Math.hypot(c.vx, c.vy);
      if (spd > c.speed) { c.vx = c.vx/spd * c.speed; c.vy = c.vy/spd * c.speed; }
      c.x += c.vx * tw; c.y += c.vy * tw;
      c.rot += 0.008 * tw;
      wrapEntity(c);

      if (!empActive) {
        c.spawnTimer -= tw;
        if (c.spawnTimer <= 0) {
          c.spawnTimer = c.spawnRate;
          drones.push(spawnDroneAt(c.x, c.y, 0));
        }
      }
    });
  }

  function spawnDroneAt(x, y, tier) {
    const d = spawnDrone(tier);
    d.x = x; d.y = y; return d;
  }

  function drawCarriers() {
    carriers.forEach(c => {
      ctx.save();
      ctx.translate(c.x, c.y); ctx.rotate(c.rot);
      const col = c.flash > 0 ? "#fff" : C.yellow;
      ctx.shadowBlur = 22; ctx.shadowColor = col;
      ctx.strokeStyle = col; ctx.lineWidth = 2;
      for (let ring = 0; ring < 2; ring++) {
        const rr = c.r - ring * 12;
        ctx.beginPath();
        for (let i = 0; i < 8; i++) {
          const a = (i / 8) * Math.PI * 2;
          i === 0 ? ctx.moveTo(Math.cos(a)*rr, Math.sin(a)*rr) : ctx.lineTo(Math.cos(a)*rr, Math.sin(a)*rr);
        }
        ctx.closePath(); ctx.stroke();
      }
      ctx.fillStyle = col; ctx.shadowBlur = 10;
      ctx.beginPath(); ctx.arc(0, 0, 6, 0, Math.PI * 2); ctx.fill();
      drawHpBar(0, c.r + 10, c.r * 2, c.hp, c.maxHp, C.yellow);
      ctx.restore();
    });
  }

  /* ══ Mines ════════════════════════════════════════════════════ */
  function spawnMine(x, y) {
    return {
      x: x || Math.random() * W * 0.7 + W * 0.15,
      y: y || Math.random() * H * 0.7 + H * 0.15,
      r: 16, hp: 1, maxHp: 1,
      score: 150, credits: 20,
      flash: 0, pulseTimer: 0,
      type: "mine",
    };
  }

  function updateMines() {
    mines.forEach(m => {
      m.pulseTimer++;
      if (m.flash > 0) m.flash--;
      const dist = Math.hypot(ship.x - m.x, ship.y - m.y);
      if (dist < m.r + 22 && ship.invincible <= 0) {
        spawnParticles(m.x, m.y, C.red, 25, 5);
        spawnShockwave(m.x, m.y, 60, C.red);
        damageShip(35);
        mines.splice(mines.indexOf(m), 1);
      }
    });
  }

  function drawMines() {
    mines.forEach(m => {
      const pulse = Math.sin(m.pulseTimer * 0.08) * 0.5 + 0.5;
      ctx.save();
      ctx.translate(m.x, m.y);
      const col = m.flash > 0 ? "#fff" : C.red;
      ctx.globalAlpha = 0.25 + pulse * 0.4;
      ctx.fillStyle = C.red;
      ctx.beginPath(); ctx.arc(0, 0, m.r * 1.8, 0, Math.PI * 2); ctx.fill();
      ctx.globalAlpha = 1;
      ctx.shadowBlur = 16 + pulse * 10; ctx.shadowColor = col;
      ctx.strokeStyle = col; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.arc(0, 0, m.r, 0, Math.PI * 2); ctx.stroke();
      for (let i = 0; i < 8; i++) {
        const a = (i / 8) * Math.PI * 2;
        ctx.beginPath();
        ctx.moveTo(Math.cos(a) * m.r, Math.sin(a) * m.r);
        ctx.lineTo(Math.cos(a) * (m.r + 6), Math.sin(a) * (m.r + 6));
        ctx.stroke();
      }
      ctx.restore();
    });
  }

  /* ══ Boss ═════════════════════════════════════════════════════ */
  const BOSS_DEFS = [
    {
      name: "GRAVITON MK-I",
      sub:  "SECTOR 1 COMMANDER",
      hp: 60, r: 52, speed: 1.1, col: C.magenta,
      phases: [
        { hpThreshold: 1.0, fireRate: 90,  bulletCount: 4, spread: Math.PI * 2 / 4 },
        { hpThreshold: 0.5, fireRate: 55,  bulletCount: 6, spread: Math.PI * 2 / 6 },
        { hpThreshold: 0.25,fireRate: 35,  bulletCount: 8, spread: Math.PI * 2 / 8 },
      ],
      score: 5000, credits: 500,
    },
    {
      name: "NEUTRON COLOSSUS",
      sub:  "SECTOR 2 WARLORD",
      hp: 120, r: 64, speed: 0.9, col: C.orange,
      phases: [
        { hpThreshold: 1.0, fireRate: 75,  bulletCount: 5, spread: Math.PI * 2 / 5 },
        { hpThreshold: 0.6, fireRate: 48,  bulletCount: 8, spread: Math.PI * 2 / 8 },
        { hpThreshold: 0.3, fireRate: 28,  bulletCount: 12,spread: Math.PI * 2 / 12 },
      ],
      score: 10000, credits: 1000,
    },
    {
      name: "VOID SOVEREIGN",
      sub:  "FINAL HARBINGER",
      hp: 200, r: 80, speed: 0.7, col: C.red,
      phases: [
        { hpThreshold: 1.0, fireRate: 60,  bulletCount: 6,  spread: Math.PI * 2 / 6 },
        { hpThreshold: 0.6, fireRate: 38,  bulletCount: 10, spread: Math.PI * 2 / 10 },
        { hpThreshold: 0.3, fireRate: 22,  bulletCount: 16, spread: Math.PI * 2 / 16 },
      ],
      score: 20000, credits: 2000,
    },
  ];

  function spawnBoss(sectorIdx) {
    const def = BOSS_DEFS[Math.min(sectorIdx, BOSS_DEFS.length - 1)];
    boss = {
      x: W / 2, y: H / 4,
      vx: 0, vy: 0,
      r: def.r, hp: def.hp, maxHp: def.hp,
      speed: def.speed, col: def.col,
      score: def.score, credits: def.credits,
      name: def.name, def,
      phase: 0, fireTimer: 0,
      rot: 0, flash: 0,
      orbitAngle: 0,
      type: "boss",
    };
  }

  function getBossPhase() {
    if (!boss) return null;
    const hpFrac = boss.hp / boss.maxHp;
    const phases = boss.def.phases;
    let ph = phases[0];
    for (let i = phases.length - 1; i >= 0; i--) {
      if (hpFrac <= phases[i].hpThreshold) { ph = phases[i]; break; }
    }
    return ph;
  }

  function updateBoss() {
    if (!boss) return;
    const tw = (timeWarpActive || empActive) ? 0.2 : 1;
    if (boss.flash > 0) boss.flash--;
    boss.rot += 0.015 * tw;

    // Boss movement — orbit centre, lunge at player
    boss.orbitAngle += 0.008 * tw;
    const orbitR = 180;
    const tx = W/2 + Math.cos(boss.orbitAngle) * orbitR;
    const ty = H/2 + Math.sin(boss.orbitAngle) * orbitR * 0.5;
    boss.vx += (tx - boss.x) * 0.005;
    boss.vy += (ty - boss.y) * 0.005;
    const bSpd = Math.hypot(boss.vx, boss.vy);
    if (bSpd > boss.speed) { boss.vx = boss.vx/bSpd*boss.speed; boss.vy = boss.vy/bSpd*boss.speed; }
    boss.x += boss.vx * tw; boss.y += boss.vy * tw;
    boss.x = Math.max(boss.r, Math.min(W-boss.r, boss.x));
    boss.y = Math.max(boss.r, Math.min(H-boss.r, boss.y));

    if (!empActive) {
      boss.fireTimer -= tw;
      const ph = getBossPhase();
      if (boss.fireTimer <= 0) {
        boss.fireTimer = Math.max(15, ph.fireRate - waveGlobal * 1.5);
        for (let i = 0; i < ph.bulletCount; i++) {
          const a = (i / ph.bulletCount) * Math.PI * 2 + boss.rot;
          fireEnemyBullet(boss.x, boss.y, a, boss.col, 5.5);
        }
      }
    }

    // Phase check
    const hpFrac = boss.hp / boss.maxHp;
    const newPhase = boss.def.phases.filter(p => hpFrac <= p.hpThreshold).length - 1;
    if (newPhase > boss.phase) {
      boss.phase = newPhase;
      spawnShockwave(boss.x, boss.y, boss.r * 2, boss.col);
      spawnParticles(boss.x, boss.y, boss.col, 30, 5);
    }
  }

  function drawBoss() {
    if (!boss) return;
    ctx.save();
    ctx.translate(boss.x, boss.y);
    const col = boss.flash > 0 ? "#fff" : boss.col;
    ctx.shadowBlur = 30; ctx.shadowColor = col;

    // Outer rings
    for (let ring = 0; ring < 3; ring++) {
      ctx.save();
      ctx.rotate(boss.rot * (ring % 2 === 0 ? 1 : -1) + ring * 0.4);
      const rr = boss.r - ring * 14;
      const sides = 8 - ring;
      ctx.strokeStyle = col + (ring === 0 ? "ff" : ring === 1 ? "aa" : "55");
      ctx.lineWidth = 2 - ring * 0.4;
      ctx.beginPath();
      for (let i = 0; i < sides; i++) {
        const a = (i / sides) * Math.PI * 2;
        i === 0 ? ctx.moveTo(Math.cos(a)*rr, Math.sin(a)*rr) : ctx.lineTo(Math.cos(a)*rr, Math.sin(a)*rr);
      }
      ctx.closePath(); ctx.stroke();
      ctx.restore();
    }

    // Core
    const cg = ctx.createRadialGradient(0, 0, 0, 0, 0, 28);
    cg.addColorStop(0, col + "cc");
    cg.addColorStop(1, col + "00");
    ctx.fillStyle = cg;
    ctx.beginPath(); ctx.arc(0, 0, 28, 0, Math.PI * 2); ctx.fill();

    // Phase indicator
    const ph = getBossPhase();
    const phIdx = boss.def.phases.indexOf(ph);

    // HP bar
    const bw = boss.r * 2.5;
    ctx.fillStyle = "#ffffff11";
    ctx.fillRect(-bw/2, boss.r + 14, bw, 7);
    ctx.fillStyle = phIdx === 0 ? col : phIdx === 1 ? C.orange : C.red;
    ctx.fillRect(-bw/2, boss.r + 14, bw * (boss.hp / boss.maxHp), 7);

    // Boss name
    ctx.font = "9px 'Courier New'";
    ctx.fillStyle = col;
    ctx.textAlign = "center";
    ctx.fillText(boss.name, 0, boss.r + 32);

    ctx.restore();
  }

  function damageBoss(amt) {
    if (!boss) return;
    boss.hp -= amt;
    boss.flash = 8;
    addScore(Math.round(boss.score / boss.maxHp * amt), boss.x, boss.y);
    if (boss.hp <= 0) {
      bossesDefeated++;
      spawnParticles(boss.x, boss.y, boss.col, 80, 8);
      spawnShockwave(boss.x, boss.y, boss.r * 3, boss.col);
      addCredits(boss.credits, boss.x, boss.y);
      addScore(boss.score, boss.x, boss.y - 40);
      boss = null;
      checkWaveComplete();
    }
  }

  /* ══ Enemy bullet helper ══════════════════════════════════════ */
  function fireEnemyBullet(x, y, angle, col, spd = 6) {
    if (timeWarpActive) return; // Time warp blocks enemy fire
    enemyBullets.push({
      x, y,
      vx: Math.cos(angle) * spd,
      vy: Math.sin(angle) * spd,
      life: 80, col, r: 3,
    });
  }

  /* ══ Power-ups ════════════════════════════════════════════════ */
  const PU_TYPES = [
    { type:"shield",   col: C.cyan,    weight: 30 },
    { type:"hull",     col: C.green,   weight: 20 },
    { type:"rapid",    col: C.yellow,  weight: 18 },
    { type:"missile",  col: C.orange,  weight: 12 },
    { type:"special",  col: C.purple,  weight: 10 },
    { type:"credit",   col: C.yellow,  weight: 25 },
    { type:"score",    col: C.white,   weight: 15 },
  ];
  const PU_TOTAL_W = PU_TYPES.reduce((s, p) => s + p.weight, 0);

  function randomPuType() {
    let r = Math.random() * PU_TOTAL_W;
    for (const p of PU_TYPES) { r -= p.weight; if (r <= 0) return p; }
    return PU_TYPES[0];
  }

  function maybePowerup(x, y, chance = 0.3) {
    if (Math.random() > chance) return;
    const t = randomPuType();
    powerups.push({ x, y, vx:(Math.random()-0.5)*1.5, vy:(Math.random()-0.5)*1.5, type:t.type, col:t.col, life:420, angle:0, r:11 });
  }

  function updatePowerups() {
    for (let i = powerups.length - 1; i >= 0; i--) {
      const p = powerups[i];
      p.x += p.vx; p.y += p.vy;
      p.angle += 0.05; p.life--;
      if (p.life <= 0) { powerups.splice(i, 1); continue; }
      if (!ship.dead && Math.hypot(ship.x - p.x, ship.y - p.y) < p.r + 18) {
        collectPowerup(p);
        spawnParticles(p.x, p.y, p.col, 14, 3);
        powerups.splice(i, 1);
      }
    }
  }

  function collectPowerup(p) {
    switch (p.type) {
      case "shield":
        ship.shield = Math.min(SHIELD_MAX, ship.shield + 40);
        spawnFloatText(ship.x, ship.y - 35, "+SHIELD", C.cyan); break;
      case "hull":
        ship.hull = Math.min(getMaxHull(), ship.hull + 25);
        spawnFloatText(ship.x, ship.y - 35, "+HULL", C.green); break;
      case "rapid":
        fireCooldown = 0;
        spawnFloatText(ship.x, ship.y - 35, "RAPID FIRE", C.yellow); break;
      case "missile":
        missiles = Math.min(getMissileMax() || 6, missiles + 2);
        spawnFloatText(ship.x, ship.y - 35, "+2 MISSILES", C.orange); break;
      case "special":
        specialCharge = SPECIAL_MAX;
        spawnFloatText(ship.x, ship.y - 35, "SPECIAL READY", C.purple); break;
      case "credit":
        const cr = (30 + Math.random() * 40 | 0);
        addCredits(cr, p.x, p.y); return;
      case "score":
        const sc = 500 * combo;
        addScore(sc, p.x, p.y); return;
    }
    addScore(50 * combo, p.x, p.y);
    updateBars(); updateHUD();
  }

  function drawPowerups() {
    powerups.forEach(p => {
      const a = p.life < 90 ? p.life / 90 : 1;
      ctx.save();
      ctx.globalAlpha = a;
      ctx.translate(p.x, p.y); ctx.rotate(p.angle);
      ctx.shadowBlur = 16; ctx.shadowColor = p.col;
      ctx.strokeStyle = p.col; ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(0, -p.r); ctx.lineTo(p.r, 0);
      ctx.lineTo(0, p.r); ctx.lineTo(-p.r, 0);
      ctx.closePath(); ctx.stroke();
      ctx.fillStyle = p.col + "22"; ctx.fill();
      ctx.fillStyle = p.col; ctx.shadowBlur = 6;
      ctx.beginPath(); ctx.arc(0, 0, 3, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    });
  }

  /* ══ Particles ════════════════════════════════════════════════ */
  function spawnParticles(x, y, col, count = 12, speed = 3, tiny = false) {
    for (let i = 0; i < count; i++) {
      const ang = Math.random() * Math.PI * 2;
      const spd = Math.random() * speed + 0.4;
      const life = 35 + Math.random() * 30;
      particles.push({ x, y, vx: Math.cos(ang)*spd, vy: Math.sin(ang)*spd, life, maxLife: life, col, r: tiny ? Math.random()*1.5+0.5 : Math.random()*2.5+0.8 });
    }
  }
  function updateParticles() {
    for (let i = particles.length - 1; i >= 0; i--) {
      const p = particles[i];
      p.x += p.vx; p.y += p.vy;
      p.vx *= 0.95; p.vy *= 0.95;
      p.life--;
      if (p.life <= 0) particles.splice(i, 1);
    }
  }
  function drawParticles() {
    particles.forEach(p => {
      ctx.save();
      ctx.globalAlpha = p.life / p.maxLife;
      ctx.shadowBlur = 7; ctx.shadowColor = p.col;
      ctx.fillStyle = p.col;
      ctx.beginPath(); ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2); ctx.fill();
      ctx.restore();
    });
  }

  /* ══ Collisions ═══════════════════════════════════════════════ */
  function checkCollisions() {
    if (ship.dead) return;

    // Player bullets vs enemies
    for (let bi = bullets.length - 1; bi >= 0; bi--) {
      const b = bullets[bi];
      let hit = false;

      for (let ai = asteroids.length - 1; ai >= 0; ai--) {
        const a = asteroids[ai];
        if (Math.hypot(b.x - a.x, b.y - a.y) < a.r + b.r) {
          damageEnemy(a, b.dmg);
          bullets.splice(bi, 1); hit = true; break;
        }
      }
      if (hit) continue;

      for (let di = drones.length - 1; di >= 0; di--) {
        const d = drones[di];
        if (Math.hypot(b.x - d.x, b.y - d.y) < d.r + b.r) {
          damageEnemy(d, b.dmg);
          bullets.splice(bi, 1); hit = true; break;
        }
      }
      if (hit) continue;

      for (let ci = carriers.length - 1; ci >= 0; ci--) {
        const c = carriers[ci];
        if (Math.hypot(b.x - c.x, b.y - c.y) < c.r + b.r) {
          damageEnemy(c, b.dmg);
          bullets.splice(bi, 1); hit = true; break;
        }
      }
      if (hit) continue;

      for (let mi = mines.length - 1; mi >= 0; mi--) {
        const m = mines[mi];
        if (Math.hypot(b.x - m.x, b.y - m.y) < m.r + b.r) {
          damageEnemy(m, 999);
          spawnShockwave(m.x, m.y, 55, C.red);
          bullets.splice(bi, 1); hit = true; break;
        }
      }
      if (hit) continue;

      if (boss && Math.hypot(b.x - boss.x, b.y - boss.y) < boss.r + b.r) {
        damageBoss(b.dmg);
        bullets.splice(bi, 1);
      }
    }

    // Enemy bullets vs ship
    if (ship.invincible <= 0) {
      for (let bi = enemyBullets.length - 1; bi >= 0; bi--) {
        if (Math.hypot(ship.x - enemyBullets[bi].x, ship.y - enemyBullets[bi].y) < 16) {
          damageShip(14);
          enemyBullets.splice(bi, 1);
        }
      }
    }

    // Ship vs asteroids
    if (ship.invincible <= 0) {
      for (let ai = asteroids.length - 1; ai >= 0; ai--) {
        const a = asteroids[ai];
        if (Math.hypot(ship.x - a.x, ship.y - a.y) < a.r + 13) {
          damageShip(20);
          a.vx *= -0.6; a.vy *= -0.6;
          break;
        }
      }
    }

    // Ship vs drones
    if (ship.invincible <= 0) {
      drones.forEach(d => {
        if (Math.hypot(ship.x - d.x, ship.y - d.y) < d.r + 13) damageShip(22);
      });
    }

    // Ship vs boss
    if (boss && ship.invincible <= 0 && Math.hypot(ship.x - boss.x, ship.y - boss.y) < boss.r + 13) {
      damageShip(30);
    }
  }

  /* ══ Damage helpers ═══════════════════════════════════════════ */
  function damageEnemy(e, dmg) {
    e.hp -= dmg;
    e.flash = 8;
    if (e.hp <= 0) killEnemy(e);
  }

  function killEnemy(e) {
    spawnParticles(e.x, e.y, e.col || C.cyan, 18, 4);
    maybePowerup(e.x, e.y, 0.28);
    addScore(e.score * combo, e.x, e.y - 20);
    addCredits(e.credits, e.x, e.y);
    increaseCombo();
    totalKills++;

    switch (e.type) {
      case "asteroid": asteroidsDestroyed++;
        if (e.splits > 0 && e.tier < 2) {
          for (let s = 0; s < e.splits; s++) {
            const child = spawnAsteroid(e.tier + 1, e.x, e.y,
              (Math.random()-0.5)*3 + e.vx*0.4,
              (Math.random()-0.5)*3 + e.vy*0.4);
            asteroids.push(child);
          }
        }
        asteroids.splice(asteroids.indexOf(e), 1); break;
      case "drone":
        dronesDestroyed++;
        drones.splice(drones.indexOf(e), 1); break;
      case "carrier":
        carriers.splice(carriers.indexOf(e), 1); break;
      case "mine":
        mines.splice(mines.indexOf(e), 1); break;
    }
    checkWaveComplete();
    updateHUD();
  }

  function damageShip(amt) {
    if (ship.dead || ship.invincible > 0) return;
    // Shield absorbs first
    if (ship.shield > 0) {
      const absorbed = Math.min(ship.shield, amt);
      ship.shield -= absorbed;
      amt -= absorbed;
    }
    if (amt > 0) { ship.hull -= amt; }
    ship.invincible = 80;
    comboReset();
    spawnParticles(ship.x, ship.y, C.cyan, 10, 2.5);
    updateBars();

    if (ship.hull <= 0) {
      ship.hull = 0; ship.dead = true;
      spawnParticles(ship.x, ship.y, C.cyan,    40, 6);
      spawnParticles(ship.x, ship.y, C.magenta, 25, 5);
      spawnShockwave(ship.x, ship.y, 80, C.cyan);
      setTimeout(showGameOver, 1400);
    }
  }

  /* ══ Score & Credits ══════════════════════════════════════════ */
  function addScore(amt, x, y) {
    score += Math.round(amt);
    if (x !== undefined) spawnFloatText(x, y, "+" + Math.round(amt), C.white, 12);
    updateHUD();
  }
  function addCredits(amt, x, y) {
    amt = Math.round(amt);
    credits += amt;
    if (x !== undefined) spawnFloatText(x, y + 14, "+" + amt + "cr", C.yellow, 11);
    updateHUD();
  }

  /* ══ Combo system ═════════════════════════════════════════════ */
  function increaseCombo() {
    comboTimer = COMBO_DECAY;
    combo = Math.min(combo + 1, 10);
    if (combo > 1) spawnFloatText(ship.x, ship.y - 55, "x" + combo + " COMBO", C.magenta, 14);
    updateHUD();
  }
  function comboReset() {
    combo = 1; comboTimer = 0; updateHUD();
  }
  function tickCombo() {
    if (comboTimer > 0) { comboTimer--; if (comboTimer === 0) comboReset(); }
  }

  /* ══ Wave objective system ════════════════════════════════════ */
  function generateObjective(w) {
    const opts = [
      { desc: "Destroy all asteroids without getting hit",   key:"nohit",    bonus:400 },
      { desc: "Kill 5 drones",                               key:"kill5d",   bonus:300 },
      { desc: "Finish wave with full shield",                key:"fullshield",bonus:350 },
      { desc: "Score a 5x combo",                            key:"combo5",   bonus:500 },
      { desc: "Destroy carrier within 30 seconds",           key:"carrier30",bonus:600 },
    ];
    const o = opts[w % opts.length];
    waveObjectives = { ...o, achieved: false, failed: false };
    objProgress = { hitCount: 0, droneKills: 0, timeStart: frameCount };
  }

  function checkObjectiveProgress() {
    if (!waveObjectives.key || waveObjectives.achieved || waveObjectives.failed) return;
    switch (waveObjectives.key) {
      case "nohit":
        if (objProgress.hitCount > 0) waveObjectives.failed = true; break;
      case "kill5d":
        if (objProgress.droneKills >= 5) waveObjectives.achieved = true; break;
      case "fullshield":
        break; // Checked on wave complete
      case "combo5":
        if (combo >= 5) waveObjectives.achieved = true; break;
    }
  }

  /* ══ HUD ══════════════════════════════════════════════════════ */
  function updateHUD() {
    hudScore.textContent   = score.toLocaleString();
    hudWave.textContent    = sector + " — " + waveInSector;
    hudCredits.textContent = credits;
    hudCombo.textContent   = "x" + combo;
    missileCount.textContent = missiles + (getMissileMax() > 0 ? "/" + getMissileMax() : "—");
    specialName.textContent  = SPECIALS[currentSpecial % SPECIALS.length];
  }
  function updateBars() {
    const shPct  = Math.max(0, ship.shield / SHIELD_MAX * 100);
    const huPct  = Math.max(0, ship.hull / getMaxHull() * 100);
    const spPct  = specialCharge / SPECIAL_MAX * 100;
    barShield.style.width  = shPct + "%";
    barHull.style.width    = huPct + "%";
    barSpecial.style.width = spPct + "%";
    if (huPct < 25) {
      barHull.style.background = "linear-gradient(90deg, #ff2244, #ff0000)";
      barHull.style.boxShadow  = "0 0 8px #ff2244";
    } else {
      barHull.style.background = "linear-gradient(90deg, #ff6600, #ff2244)";
      barHull.style.boxShadow  = "0 0 6px #ff6600";
    }
  }
  function renderWeaponSlots() {
    hudWeapSlots.innerHTML = "";
    WEAPONS.forEach((w, i) => {
      const el = document.createElement("div");
      el.className = "weapon-slot" + (i === currentWeapon ? " active" : "") + (!weaponsUnlocked[i] ? " disabled" : "");
      el.textContent = "[" + (i+1) + "] " + w.name;
      hudWeapSlots.appendChild(el);
    });
  }

  /* ══ Shop ═════════════════════════════════════════════════════ */
  let shopFromMenu = false;

  function openShopMenu() {
    shopFromMenu = true;
    renderShop(false);
    showScreen("shop");
  }

  function openShopBetweenWaves() {
    shopFromMenu = false;
    renderShop(true);
    shopWaveInfo.textContent = "SECTOR " + sector + " — WAVE " + waveInSector + " COMPLETE";
    showScreen("shop");
  }

  function renderShop(showObjectives) {
    shopCreditsV.textContent = credits;
    shopGrid.innerHTML = "";

    const upgrList = [
      { id:"thrustPower",   name:"ENGINE BOOST",    icon:"»" },
      { id:"fireRate",      name:"FIRE CONTROL",    icon:"+" },
      { id:"shieldRegen",   name:"SHIELD MATRIX",   icon:"O" },
      { id:"hullPlating",   name:"HULL PLATING",    icon:"#" },
      { id:"bulletDamage",  name:"WEAPON CORE",     icon:"*" },
      { id:"missileSystem", name:"MISSILE BAY",     icon:"M" },
      { id:"specialCharge", name:"ENERGY CORE",     icon:"E" },
      { id:"weaponScatter", name:"SCATTER CANNON",  icon:"~" },
      { id:"weaponPlasma",  name:"PLASMA LANCE",    icon:"!" },
    ];

    upgrList.forEach(u => {
      const up = upgrades[u.id];
      const maxed = up.level >= up.max;
      const cost  = maxed ? 0 : up.cost[up.level];
      const canBuy= !maxed && credits >= cost;

      const card = document.createElement("div");
      card.className = "shop-card" + (maxed ? " maxed" : !canBuy ? " disabled" : "");

      const dots = Array.from({ length: up.max }, (_, i) =>
        `<div class="shop-dot${i < up.level ? " filled" : ""}"></div>`
      ).join("");

      card.innerHTML = `
        <div class="shop-card-level">LVL ${up.level}/${up.max}</div>
        <div class="shop-card-name">${u.icon} ${u.name}</div>
        <div class="shop-card-desc">${up.desc}</div>
        <div class="shop-level-dots">${dots}</div>
        <div class="shop-card-cost">${maxed ? "MAXED" : cost + " CR"}</div>
      `;

      if (!maxed && canBuy) {
        card.addEventListener("click", () => {
          if (credits < cost) return;
          credits -= cost;
          up.level++;
          // Unlock weapons
          if (u.id === "weaponScatter") weaponsUnlocked[1] = true;
          if (u.id === "weaponPlasma")  weaponsUnlocked[2] = true;
          // Restore hull if upgraded
          if (u.id === "hullPlating" && ship) ship.hull = Math.min(getMaxHull(), ship.hull + 30);
          renderShop(showObjectives);
          renderWeaponSlots();
          updateHUD();
        });
      }
      shopGrid.appendChild(card);
    });

    if (showObjectives && waveObjectives.key) {
      const achieved = waveObjectives.achieved;
      const failed   = waveObjectives.failed;
      shopObjectives.innerHTML =
        `OBJECTIVE: <span class="${achieved ? "obj-done" : ""}">${waveObjectives.desc}</span><br>` +
        `BONUS: <span class="col-yellow">${waveObjectives.bonus} CR</span> — ` +
        `<span class="${achieved ? "obj-done col-green" : "col-magenta"}">${achieved ? "ACHIEVED" : failed ? "FAILED" : "PENDING"}</span>`;
      if (achieved && !waveObjectives._paid) {
        waveObjectives._paid = true;
        credits += waveObjectives.bonus;
        shopCreditsV.textContent = credits;
      }
    } else {
      shopObjectives.innerHTML = "";
    }
  }

  function shopContinue() {
    if (shopFromMenu) { showScreen("menu"); return; }
    proceedToNextWave();
  }

  /* ══ Waves & Sectors ══════════════════════════════════════════ */
  const SECTOR_NAMES = ["ALPHA QUADRANT", "BETA QUADRANT", "OMEGA SECTOR"];
  const WAVE_SUBS = [
    "LIGHT CONTACT", "HOSTILES DETECTED", "SUPPRESSION REQUIRED",
    "HEAVY RESISTANCE", "BOSS SECTOR INCOMING",
    "PERIMETER BREACHED", "ENEMY SURGE", "FULL ASSAULT",
    "CRITICAL THREAT", "FINAL APPROACH",
    "OMEGA PROTOCOL ACTIVE", "NO RETREAT", "ALL UNITS ENGAGED",
    "SYSTEM OVERRIDE", "ENDGAME",
  ];

  function buildWaveConfig(g) {
    // g = global wave number 1..15
    const s = Math.ceil(g / WAVES_PER_SECTOR); // sector 1-3
    const w = ((g - 1) % WAVES_PER_SECTOR) + 1; // wave in sector 1-5
    const isBossWave = (w === WAVES_PER_SECTOR);

    if (isBossWave) return { boss: s - 1, asteroids:[], drones:[], carriers:0, mines:0 };

    const diff = g;
    return {
      boss: null,
      asteroids: [
        Math.max(0, 2 + Math.floor(diff * 0.5)),
        Math.max(0, Math.floor(diff * 0.3)),
        Math.max(0, Math.floor(diff * 0.2)),
      ],
      drones: [
        Math.max(0, Math.floor(diff * 0.35)),
        g >= 6 ? Math.max(0, Math.floor((diff - 5) * 0.2)) : 0,
        g >= 10 ? Math.max(0, Math.floor((diff - 9) * 0.15)) : 0,
      ],
      carriers: g >= 5 ? Math.min(2, Math.floor((g - 4) * 0.25)) : 0,
      mines: g >= 3 ? Math.min(4, Math.floor((g - 2) * 0.4)) : 0,
    };
  }

  function startWave(g) {
    waveGlobal     = g;
    sector         = Math.ceil(g / WAVES_PER_SECTOR);
    waveInSector   = ((g - 1) % WAVES_PER_SECTOR) + 1;
    waveActive     = false;
    waveAnnouncing = true;

    bullets = []; enemyBullets = [];
    asteroids = []; drones = []; carriers = []; mines = []; boss = null;
    powerups = []; missileObjects = [];

    const cfg = buildWaveConfig(g);
    generateObjective(g);

    const isBossWave = waveInSector === WAVES_PER_SECTOR;

    if (isBossWave) {
      // Boss wave — show boss warning then announce
      bossTitle.textContent = BOSS_DEFS[Math.min(sector - 1, BOSS_DEFS.length - 1)].name;
      bossSub.textContent   = BOSS_DEFS[Math.min(sector - 1, BOSS_DEFS.length - 1)].sub;
      showScreen("boss");
      setTimeout(() => {
        if (!gameRunning) return;
        spawnBoss(sector - 1);
        showWaveAnnounce(g, cfg, true, () => { waveAnnouncing = false; waveActive = true; showScreen("hud"); });
      }, 2500);
    } else {
      cfg.asteroids.forEach((cnt, tier) => { for (let i=0;i<cnt;i++) asteroids.push(spawnAsteroid(tier)); });
      cfg.drones.forEach((cnt, tier)    => { for (let i=0;i<cnt;i++) drones.push(spawnDrone(tier)); });
      for (let i = 0; i < cfg.carriers; i++) carriers.push(spawnCarrier());
      for (let i = 0; i < cfg.mines;    i++) mines.push(spawnMine());
      showWaveAnnounce(g, cfg, false, () => { waveAnnouncing = false; waveActive = true; showScreen("hud"); });
    }

    hudWave.textContent = sector + " — " + waveInSector;
    updateHUD();
  }

  function showWaveAnnounce(g, cfg, isBoss, cb) {
    waveSector.textContent   = SECTOR_NAMES[sector - 1] || "";
    waveText.textContent     = isBoss ? "BOSS WAVE" : "WAVE " + waveInSector;
    waveSub.textContent      = WAVE_SUBS[Math.min(g - 1, WAVE_SUBS.length - 1)];
    waveObjective.textContent = waveObjectives.desc ? "OBJECTIVE: " + waveObjectives.desc : "";
    showScreen("hud", "wave");
    setTimeout(cb, 2200);
  }

  function checkWaveComplete() {
    if (!waveActive) return;
    const allDead = asteroids.length === 0 && drones.length === 0 && carriers.length === 0 && mines.length === 0 && boss === null;
    if (!allDead) return;

    waveActive = false;

    // Check objective completion
    if (waveObjectives.key === "fullshield" && ship.shield >= SHIELD_MAX) waveObjectives.achieved = true;
    if (waveObjectives.key === "nohit" && objProgress.hitCount === 0) waveObjectives.achieved = true;

    const isLastWave = waveGlobal >= TOTAL_SECTORS * WAVES_PER_SECTOR;
    const isSectorEnd = waveInSector === WAVES_PER_SECTOR;

    // Wave complete bonus
    const waveBonus = waveInSector * sector * 500;
    addCredits(waveBonus * 0.2 | 0, ship.x, ship.y - 40);
    addScore(waveBonus, ship.x, ship.y - 60);

    if (isLastWave) {
      setTimeout(() => { if (gameRunning) showWin(); }, 1600);
    } else if (isSectorEnd) {
      setTimeout(() => { if (gameRunning) openShopBetweenWaves(); }, 1600);
    } else {
      setTimeout(() => { if (gameRunning) openShopBetweenWaves(); }, 1600);
    }
  }

  function proceedToNextWave() {
    if (waveGlobal >= TOTAL_SECTORS * WAVES_PER_SECTOR) { showWin(); return; }
    startWave(waveGlobal + 1);
  }

  /* ══ Game over / Win ══════════════════════════════════════════ */
  function showGameOver() {
    gameRunning = false;
    const isNew = score > bestScore;
    if (isNew) bestScore = score;
    goScore.textContent = score.toLocaleString();
    goBest.textContent  = isNew ? "NEW RECORD" : "BEST: " + bestScore.toLocaleString();
    goStats.innerHTML =
      "SECTOR REACHED: " + sector + "<br>" +
      "WAVES COMPLETED: " + (waveGlobal - 1) + "<br>" +
      "TOTAL KILLS: " + totalKills + "<br>" +
      "ASTEROIDS: " + asteroidsDestroyed + " &nbsp;|&nbsp; DRONES: " + dronesDestroyed + "<br>" +
      "BOSSES DEFEATED: " + bossesDefeated + "<br>" +
      "CREDITS EARNED: " + credits;
    showScreen("gameover");
  }

  function showWin() {
    gameRunning = false;
    const isNew = score > bestScore;
    if (isNew) bestScore = score;
    winScore.textContent = score.toLocaleString();
    winBest.textContent  = isNew ? "NEW RECORD" : "BEST: " + bestScore.toLocaleString();
    winSub.textContent   = "ALL SECTORS CLEARED — VOID RUNNER VICTORIOUS";
    showScreen("win");
  }

  /* ══ Pause ════════════════════════════════════════════════════ */
  function togglePause() {
    if (!gameRunning) return;
    gamePaused = !gamePaused;
    if (gamePaused) showScreen("hud", "pause");
    else showScreen("hud");
  }
  function resumeGame() { gamePaused = false; showScreen("hud"); }

  /* ══ Start game ═══════════════════════════════════════════════ */
  function startGame() {
    score = 0; credits = 200; // Starting credits
    sector = 1; waveInSector = 0; waveGlobal = 0;
    combo = 1; comboTimer = 0;
    totalKills = 0; asteroidsDestroyed = 0; dronesDestroyed = 0; bossesDefeated = 0;
    currentWeapon = 0; weaponsUnlocked = [true, false, false];
    fireCooldown = 0; specialCharge = 0; specialCooldown = 0;
    shieldBurstCD = 0; missiles = 0;
    timeWarpActive = false; empActive = false;
    currentSpecial = 0;
    frameCount = 0;
    bullets = []; enemyBullets = []; asteroids = []; drones = [];
    carriers = []; mines = []; boss = null;
    particles = []; powerups = []; shockwaves = []; missileObjects = [];

    // Reset upgrades
    Object.keys(upgrades).forEach(k => { upgrades[k].level = 0; });

    ship = createShip();
    buildStars();
    renderWeaponSlots();

    gameRunning = true; gamePaused = false;
    updateHUD(); updateBars();
    startWave(1);

    if (!loopRunning) { loopRunning = true; requestAnimationFrame(loop); }
  }

  /* ══ HUD overlays on canvas ═══════════════════════════════════ */
  function drawHpBar(cx, cy, w, hp, maxHp, col) {
    // called in local (translated) context
    ctx.fillStyle = "#ffffff18";
    ctx.fillRect(cx - w/2, cy, w, 4);
    ctx.fillStyle = col;
    ctx.fillRect(cx - w/2, cy, w * (hp/maxHp), 4);
  }

  function drawCanvasHUD() {
    // Time warp overlay
    if (timeWarpActive) {
      ctx.save();
      ctx.fillStyle = "#aa00ff08";
      ctx.fillRect(0, 0, W, H);
      ctx.strokeStyle = C.purple + "44";
      ctx.lineWidth = 2;
      ctx.setLineDash([10, 20]);
      ctx.strokeRect(10, 10, W-20, H-20);
      ctx.setLineDash([]);
      ctx.restore();
    }
    // EMP overlay
    if (empActive) {
      ctx.save();
      ctx.fillStyle = "#ffe60006";
      ctx.fillRect(0, 0, W, H);
      ctx.restore();
    }

    // Combo display on canvas (centre top)
    if (combo > 1) {
      ctx.save();
      const a = Math.min(1, comboTimer / 60);
      ctx.globalAlpha = a;
      ctx.font = "bold " + clamp(16 + combo * 2, 18, 32) + "px 'Courier New'";
      ctx.textAlign = "center";
      ctx.fillStyle = C.magenta;
      ctx.shadowBlur = 12; ctx.shadowColor = C.magenta;
      ctx.fillText("x" + combo + " COMBO", W/2, 55);
      ctx.restore();
    }

    // Shield burst cooldown indicator
    if (shieldBurstCD > 0) {
      ctx.save();
      ctx.font = "9px 'Courier New'";
      ctx.textAlign = "left";
      ctx.fillStyle = C.cyan + "88";
      ctx.fillText("SHIELD BURST: " + Math.ceil(shieldBurstCD/60) + "s", 20, H - 24);
      ctx.restore();
    }

    // Boss HP bar at top
    if (boss) {
      const bw = Math.min(W * 0.6, 500);
      const bx = W/2 - bw/2;
      const by = 60;
      ctx.save();
      ctx.shadowBlur = 10; ctx.shadowColor = boss.col;
      ctx.font = "9px 'Courier New'";
      ctx.textAlign = "center";
      ctx.fillStyle = boss.col;
      ctx.fillText(boss.name, W/2, by - 6);
      ctx.fillStyle = "#ffffff10";
      ctx.fillRect(bx, by, bw, 10);
      const frac = boss.hp / boss.maxHp;
      const bCol = frac > 0.5 ? boss.col : frac > 0.25 ? C.orange : C.red;
      ctx.fillStyle = bCol;
      ctx.shadowBlur = 12;
      ctx.fillRect(bx, by, bw * frac, 10);
      ctx.restore();
    }
  }

  /* ══ Helpers ══════════════════════════════════════════════════ */
  function outOfBounds(e, margin = 30) {
    return e.x < -margin || e.x > W+margin || e.y < -margin || e.y > H+margin;
  }
  function wrapEntity(e) {
    const m = (e.r || 20) + 20;
    if (e.x < -m) e.x = W+m; if (e.x > W+m) e.x = -m;
    if (e.y < -m) e.y = H+m; if (e.y > H+m) e.y = -m;
  }
  function spawnEdge(margin = 40) {
    const side = Math.floor(Math.random() * 4);
    switch(side) {
      case 0: return { x: Math.random()*W, y: -margin };
      case 1: return { x: W+margin,         y: Math.random()*H };
      case 2: return { x: Math.random()*W, y: H+margin };
      default:return { x: -margin,          y: Math.random()*H };
    }
  }
  function clamp(v, min, max) { return Math.max(min, Math.min(max, v)); }

  /* ══ Main loop ════════════════════════════════════════════════ */
  let loopRunning = false;

  function loop() {
    requestAnimationFrame(loop);

    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = "#03050e";
    ctx.fillRect(0, 0, W, H);

    drawBackground();

    if (!gameRunning || gamePaused) return;

    frameCount++;
    tickCombo();
    checkObjectiveProgress();

    if (!waveAnnouncing) {
      if (fireCooldown > 0) fireCooldown--;
      if (keys["Space"]) fireBullet();
      updateShip();
      updateBullets();
      updateAsteroids();
      updateDrones();
      updateCarriers();
      updateMines();
      updateBoss();
      updatePowerups();
      updateParticles();
      updateShockwaves();
      checkCollisions();
    }

    drawShockwaves();
    drawParticles();
    drawMines();
    drawAsteroids();
    drawCarriers();
    drawDrones();
    drawBoss();
    drawPowerups();
    drawBullets();
    drawShip();
    drawCanvasHUD();
  }

  /* ── Boot ───────────────────────────────────────────────────── */
  buildStars();
  bestDisp.textContent = "BEST SCORE: 0";
  showScreen("menu");
  loopRunning = true;
  requestAnimationFrame(loop);

})();
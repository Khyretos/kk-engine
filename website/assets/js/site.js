// engine.kreative-kompas.com: small, dependency-free interactions.
(function () {
  "use strict";
  const reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // Light / dark, remembered.
  const toggle = document.querySelector(".theme-toggle");
  if (toggle) toggle.addEventListener("click", function () {
    const dark = document.documentElement.classList.toggle("dark");
    try { localStorage.setItem("theme", dark ? "dark" : "light"); } catch (e) { /* private mode: not remembered */ }
  });

  // Mobile menu.
  const menu = document.querySelector(".menu-toggle"), nav = document.querySelector(".nav");
  if (menu && nav) menu.addEventListener("click", function () {
    const open = nav.classList.toggle("open");
    menu.setAttribute("aria-expanded", open ? "true" : "false");
  });

  // Fade sections in as they scroll into view.
  const reveals = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && !reduced) {
    const io = new IntersectionObserver(function (entries) {
      entries.forEach(function (e) { if (e.isIntersecting) { e.target.classList.add("visible"); io.unobserve(e.target); } });
    }, { threshold: 0.12 });
    reveals.forEach(function (el) { io.observe(el); });
  } else {
    reveals.forEach(function (el) { el.classList.add("visible"); });
  }

  // Typing effect in the hero.
  const typed = document.querySelector(".typed");
  if (typed) {
    const words = typed.dataset.words.split("|");
    if (reduced) typed.textContent = words[0];
    else {
      let w = 0, i = 0, deleting = false;
      (function tick() {
        const word = words[w];
        i += deleting ? -1 : 1;
        typed.textContent = word.slice(0, i);
        let wait = deleting ? 35 : 70;
        if (!deleting && i === word.length) { deleting = true; wait = 1600; }
        else if (deleting && i === 0) { deleting = false; w = (w + 1) % words.length; wait = 300; }
        setTimeout(tick, wait);
      })();
    }
  }

  // Count the stats up when they appear.
  document.querySelectorAll("[data-count]").forEach(function (el) {
    const target = parseInt(el.dataset.count, 10);
    if (reduced || !("IntersectionObserver" in window) || target === 0) return;
    el.textContent = "0";
    const io = new IntersectionObserver(function (entries) {
      if (!entries[0].isIntersecting) return;
      io.disconnect();
      const start = performance.now();
      (function step(now) {
        const t = Math.min(1, (now - start) / 1200);
        el.textContent = String(Math.round(target * (1 - Math.pow(1 - t, 3))));
        if (t < 1) requestAnimationFrame(step);
      })(start);
    });
    io.observe(el);
  });

  // Feature image carousels: dots, and a slow auto-advance while visible.
  document.querySelectorAll("[data-carousel]").forEach(function (stack) {
    const figs = stack.querySelectorAll(".media"), dots = stack.querySelectorAll(".dots button");
    if (figs.length < 2) return;
    let current = 0, timer = null;
    function show(n) {
      figs[current].classList.remove("active"); if (dots[current]) dots[current].classList.remove("active");
      current = n;
      figs[current].classList.add("active"); if (dots[current]) dots[current].classList.add("active");
    }
    dots.forEach(function (d, n) { d.addEventListener("click", function () { show(n); clearInterval(timer); }); });
    if (reduced || !("IntersectionObserver" in window)) return;
    new IntersectionObserver(function (entries) {
      clearInterval(timer);
      if (entries[0].isIntersecting) timer = setInterval(function () { show((current + 1) % figs.length); }, 4500);
    }, { threshold: 0.4 }).observe(stack);
  });

  // Sound chips: play the engine's synthesized impacts.
  let audio = null;
  document.querySelectorAll("[data-sound]").forEach(function (chip) {
    chip.addEventListener("click", function () {
      if (audio) audio.pause();
      document.querySelectorAll(".chip.playing").forEach(function (c) { c.classList.remove("playing"); });
      audio = new Audio(chip.dataset.sound);
      chip.classList.add("playing");
      audio.addEventListener("ended", function () { chip.classList.remove("playing"); });
      audio.play().catch(function () { chip.classList.remove("playing"); });
    });
  });

  // Click a screenshot to see it large.
  const box = document.createElement("div");
  box.className = "lightbox";
  const big = document.createElement("img");
  box.appendChild(big);
  document.body.appendChild(box);
  box.addEventListener("click", function () { box.classList.remove("open"); });
  document.addEventListener("keydown", function (e) { if (e.key === "Escape") box.classList.remove("open"); });
  document.querySelectorAll(".media img").forEach(function (img) {
    img.addEventListener("click", function () { big.src = img.src; big.alt = img.alt; box.classList.add("open"); });
  });

  // A slow starfield behind the hero, like the engine's own intro.
  const canvas = document.querySelector(".stars");
  if (canvas && !reduced) {
    const ctx = canvas.getContext("2d");
    let stars = [], w = 0, h = 0;
    function resize() {
      const dpr = window.devicePixelRatio || 1;
      w = canvas.clientWidth; h = canvas.clientHeight;
      canvas.width = w * dpr; canvas.height = h * dpr;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
      stars = Array.from({ length: Math.round(w * h / 9000) }, function () {
        return { x: Math.random() * w, y: Math.random() * h, r: Math.random() * 1.4 + 0.3, s: Math.random() * 0.25 + 0.05, p: Math.random() * 6.28 };
      });
    }
    resize();
    window.addEventListener("resize", resize);
    (function frame(t) {
      ctx.clearRect(0, 0, w, h);
      const dark = document.documentElement.classList.contains("dark");
      for (const s of stars) {
        s.y -= s.s; if (s.y < -2) { s.y = h + 2; s.x = Math.random() * w; }
        const a = 0.35 + 0.35 * Math.sin(t / 900 + s.p);
        ctx.fillStyle = dark ? "rgba(220, 200, 255," + a + ")" : "rgba(140, 60, 220," + a * 0.6 + ")";
        ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, 6.2832); ctx.fill();
      }
      requestAnimationFrame(frame);
    })(0);
  }
})();

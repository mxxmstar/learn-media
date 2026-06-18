<script setup lang="ts">
import { ref } from "vue";
import Player from "./player/player.vue";

const currentPage = ref("home");

function goToPlayer() {
  currentPage.value = "player";
}

function goHome() {
  currentPage.value = "home";
}
</script>

<template>
  <!-- 主页 -->
  <div v-if="currentPage === 'home'" class="home">
    <div class="home-card">
      <div class="home-icon">🎬</div>
      <h1>Learn Media Player</h1>
      <p class="home-desc">
        基于 <strong>Tauri</strong> + <strong>Vue 3</strong> + <strong>Rust</strong> 的媒体播放器
      </p>
      <div class="home-features">
        <span>🎥 视频播放</span>
        <span>🎵 音频支持</span>
        <span>📂 本地文件</span>
        <span>⚡ AI 推理</span>
      </div>
      <button class="home-btn" @click="goToPlayer">🚀 进入播放器</button>
    </div>
  </div>

  <!-- 播放器页面 -->
  <Player v-else @back="goHome" />
</template>

<style>
* {
  margin: 0;
  padding: 0;
  box-sizing: border-box;
}

html, body, #app {
  width: 100%;
  height: 100%;
  overflow: hidden;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
  background: #1a1a2e;
  color: #eee;
}

.home {
  width: 100%;
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
}

.home-card {
  text-align: center;
  padding: 48px;
}

.home-icon {
  font-size: 5rem;
  margin-bottom: 1rem;
}

.home-card h1 {
  font-size: 2.2rem;
  font-weight: 700;
  color: #e94560;
  margin-bottom: 0.6rem;
}

.home-desc {
  font-size: 1.05rem;
  color: #a0a0b0;
  margin-bottom: 1.5rem;
}
.home-desc strong {
  color: #eee;
}

.home-features {
  display: flex;
  gap: 16px;
  justify-content: center;
  margin-bottom: 2rem;
  flex-wrap: wrap;
}
.home-features span {
  background: rgba(255, 255, 255, 0.06);
  padding: 6px 16px;
  border-radius: 20px;
  font-size: 0.9rem;
  color: #ccc;
}

.home-btn {
  background: #e94560;
  color: #fff;
  border: none;
  padding: 14px 40px;
  font-size: 1.15rem;
  font-weight: 600;
  border-radius: 10px;
  cursor: pointer;
  transition: transform 0.2s, box-shadow 0.2s;
  box-shadow: 0 4px 20px rgba(233, 69, 96, 0.3);
}
.home-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 6px 28px rgba(233, 69, 96, 0.5);
}
.home-btn:active {
  transform: translateY(0);
}
</style>
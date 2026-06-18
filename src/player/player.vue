<script setup lang="ts">
import { ref } from "vue";
import { open } from "@tauri-apps/plugin-dialog";
import { readFile } from "@tauri-apps/plugin-fs";

defineEmits<{
  back: [];
}>();

const videoPath = ref<string | null>(null);
const videoSrc = ref<string>("");
const isLoading = ref(false);
const loadError = ref<string | null>(null);

// 获取文件的 MIME 类型
function getMimeType(path: string): string {
  const ext = path.split(".").pop()?.toLowerCase();
  const map: Record<string, string> = {
    mp4: "video/mp4",
    webm: "video/webm",
    avi: "video/x-msvideo",
    mkv: "video/x-matroska",
    mov: "video/quicktime",
    wmv: "video/x-ms-wmv",
    flv: "video/x-flv",
  };
  return map[ext || ""] || "video/mp4";
}

async function openFile() {
  const selected = await open({
    multiple: false,
    filters: [
      {
        name: "视频文件",
        extensions: ["mp4", "avi", "mkv", "mov", "wmv", "flv", "webm"],
      },
      {
        name: "所有文件",
        extensions: ["*"],
      },
    ],
  });

  if (!selected) return;

  videoPath.value = selected;
  isLoading.value = true;
  loadError.value = null;

  try {
    // 读取文件为字节数组
    const bytes = await readFile(selected);
    // 创建 Blob URL
    const blob = new Blob([bytes], { type: getMimeType(selected) });

    // 释放之前的 Blob URL
    if (videoSrc.value) {
      URL.revokeObjectURL(videoSrc.value);
    }

    videoSrc.value = URL.createObjectURL(blob);
  } catch (err) {
    console.error("读取文件失败:", err);
    loadError.value = `无法读取文件: ${err}`;
  } finally {
    isLoading.value = false;
  }
}
</script>

<template>
  <div class="player">
    <!-- 顶部标题栏 -->
    <header class="player-header">
      <button class="back-btn" @click="$emit('back')">← 返回</button>
      <h1>🎬 Player</h1>
    </header>

    <!-- 视频显示区域 -->
    <div class="player-screen">
      <!-- 加载中 -->
      <div v-if="isLoading" class="player-placeholder">
        <div class="placeholder-icon">⏳</div>
        <p>正在加载视频...</p>
      </div>

      <!-- 加载出错 -->
      <div v-else-if="loadError" class="player-placeholder">
        <div class="placeholder-icon">❌</div>
        <p class="error-text">{{ loadError }}</p>
        <button class="retry-btn" @click="openFile">重新选择</button>
      </div>

      <!-- 未选择文件 -->
      <div v-else-if="!videoSrc" class="player-placeholder">
        <div class="placeholder-icon">📺</div>
        <p>拖拽视频文件到这里<br/>或点击下方按钮打开</p>
      </div>

      <!-- 播放视频 -->
      <video
        v-else
        :src="videoSrc"
        class="player-video"
        controls
        autoplay
        @error="onVideoError"
      ></video>
    </div>

    <!-- 底部控制栏 -->
    <div class="player-controls">
      <button class="ctrl-btn open-btn" @click="openFile">📂 打开文件</button>

      <div class="ctrl-group">
        <button class="ctrl-btn" disabled>⏮</button>
        <button class="ctrl-btn play-btn" disabled>▶</button>
        <button class="ctrl-btn" disabled>⏭</button>
      </div>

      <div class="ctrl-progress">
        <input type="range" min="0" max="100" value="0" disabled />
        <span class="time-label">00:00 / 00:00</span>
      </div>

      <div class="ctrl-volume">
        <span>🔊</span>
        <input type="range" min="0" max="100" value="80" disabled />
      </div>
    </div>
  </div>
</template>

<style scoped>
.player {
  display: flex;
  flex-direction: column;
  height: 100vh;
  background: #1a1a2e;
}

.player-header {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 20px;
  background: #16213e;
  border-bottom: 1px solid #0f3460;
  -webkit-user-select: none;
  user-select: none;
}
.player-header h1 {
  font-size: 1.1rem;
  font-weight: 600;
  color: #e94560;
}

.back-btn {
  background: transparent;
  border: 1px solid #0f3460;
  color: #aaa;
  padding: 4px 12px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.85rem;
  transition: all 0.2s;
}
.back-btn:hover {
  background: #0f3460;
  color: #eee;
}

.player-screen {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #0f0f23;
  position: relative;
  overflow: hidden;
}

.player-video {
  width: 100%;
  height: 100%;
  object-fit: contain;
  background: #000;
}

.player-placeholder {
  text-align: center;
  color: #555;
}
.placeholder-icon {
  font-size: 4rem;
  margin-bottom: 1rem;
  opacity: 0.4;
}
.player-placeholder p {
  font-size: 1rem;
  line-height: 1.6;
  color: #666;
}

.error-text {
  color: #e94560;
  font-size: 0.9rem;
  max-width: 400px;
  word-break: break-all;
}

.retry-btn {
  margin-top: 12px;
  background: #e94560;
  border: none;
  color: #fff;
  padding: 8px 20px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.9rem;
}
.retry-btn:hover {
  background: #d63851;
}
.error-text {
  color: #e94560;
  font-size: 0.9rem;
  max-width: 400px;
  word-break: break-all;
}

.retry-btn {
  margin-top: 12px;
  background: #e94560;
  border: none;
  color: #fff;
  padding: 8px 20px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.9rem;
}
.retry-btn:hover {
  background: #d63851;
}


.player-controls {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 20px;
  background: #16213e;
  border-top: 1px solid #0f3460;
}

.ctrl-btn {
  background: #0f3460;
  border: none;
  color: #eee;
  padding: 8px 14px;
  border-radius: 6px;
  cursor: pointer;
  font-size: 0.95rem;
  transition: background 0.2s;
}
.ctrl-btn:hover:not(:disabled) {
  background: #e94560;
}
.ctrl-btn:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}
.play-btn {
  padding: 8px 20px;
  font-size: 1.1rem;
}

.ctrl-progress {
  flex: 1;
  display: flex;
  align-items: center;
  gap: 10px;
}
.ctrl-progress input[type="range"] {
  flex: 1;
  height: 4px;
  accent-color: #e94560;
  cursor: pointer;
}
.ctrl-progress input[type="range"]:disabled {
  cursor: not-allowed;
  opacity: 0.4;
}
.time-label {
  font-size: 0.8rem;
  color: #888;
  min-width: 90px;
  text-align: right;
  font-variant-numeric: tabular-nums;
}

.ctrl-volume {
  display: flex;
  align-items: center;
  gap: 6px;
}
.ctrl-volume input[type="range"] {
  width: 70px;
  height: 4px;
  accent-color: #e94560;
  cursor: pointer;
}
.ctrl-volume input[type="range"]:disabled {
  cursor: not-allowed;
  opacity: 0.4;
}
</style>
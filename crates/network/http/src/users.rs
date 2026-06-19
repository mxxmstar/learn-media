use axum::{
    http::StatusCode,
    response::IntoResponse,
    Json,
};
use serde::{Deserialize, Serialize};
use std::sync::{Arc, Mutex};

#[derive(Serialize, Clone)]
pub struct User {
    pub id: u32,
    pub name: String,
    pub age: u8,
}

#[derive(Deserialize)]
pub struct CreateUser {
    pub name: String,
    pub age: u8,
}

/// 共享状态：用户列表
pub type UserStore = Arc<Mutex<Vec<User>>>;

/// 自定义错误类型
pub enum AppError {
    NotFound(String),
    Internal(String),
}

impl IntoResponse for AppError {
    fn into_response(self) -> axum::response::Response {
        let (status, message) = match self {
            AppError::NotFound(msg) => (StatusCode::NOT_FOUND, msg),
            AppError::Internal(msg) => (StatusCode::INTERNAL_SERVER_ERROR, msg),
        };
        (status, Json(serde_json::json!({ "error": message }))).into_response()
    }
}
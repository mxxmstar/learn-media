use axum::{
    extract::{Path, State},
    Json,
};
use crate::users::{AppError, CreateUser, User, UserStore};

pub async fn hello() -> &'static str {
    "Hello, World!"
}

pub async fn hello_name(Path(name): Path<String>) -> String {
    format!("Hello, {}!", name)
}

pub async fn post_user(State(store): State<UserStore>, Json(input): Json<CreateUser>) -> Json<User> {
    let mut users = store.lock().unwrap();
    let id = users.len() as u32 + 1;
    let user = User {
        id,
        name: input.name,
        age: input.age,
    };
    users.push(user.clone());
    tracing::info!("created user #{}: {}", user.id, user.name);
    Json(user)
}

pub async fn get_users(State(store): State<UserStore>) -> Json<Vec<User>> {
    let users = store.lock().unwrap();
    Json(users.clone())
}

pub async fn get_user_by_id(State(store): State<UserStore>, Path(id): Path<u32>) -> Result<Json<User>, AppError> {
    let users = store.lock().unwrap();
    users
        .iter()
        .find(|u| u.id == id)
        .cloned()
        .map(Json)
        .ok_or_else(|| AppError::NotFound(format!("user #{} not found", id)))
}

pub async fn json_demo() -> Json<User> {
    Json(User {
        id: 1,
        name: "Alice".to_string(),
        age: 30,
    })
}

/// Admin: 获取系统统计信息
pub async fn admin_stats(State(store): State<UserStore>) -> Json<serde_json::Value> {
    let users = store.lock().unwrap();
    Json(serde_json::json!({
        "total_users": users.len(),
        "status": "ok"
    }))
}
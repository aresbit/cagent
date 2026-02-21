// cclaw_loader.rs - Load CClaw JSON configuration directly
// SPDX-License-Identifier: MIT

use crate::config::{AutonomyConfig, ChannelsConfig, Config, GatewayConfig, MemoryConfig, TelegramConfig};
use crate::security::AutonomyLevel;
use anyhow::{Context, Result};
use serde::Deserialize;
use std::path::PathBuf;

/// CClaw JSON configuration structure
#[derive(Debug, Deserialize)]
struct CClawJsonConfig {
    api_key: Option<String>,
    default_provider: Option<String>,
    default_model: Option<String>,
    default_temperature: Option<f64>,
    memory: Option<CClawMemoryConfig>,
    autonomy: Option<CClawAutonomyConfig>,
    channels: Option<CClawChannelsConfig>,
    gateway: Option<CClawGatewayConfig>,
}

#[derive(Debug, Deserialize)]
struct CClawMemoryConfig {
    backend: String,
}

#[derive(Debug, Deserialize)]
struct CClawAutonomyConfig {
    level: u8,
    #[serde(default)]
    workspace_only: bool,
    #[serde(default)]
    max_actions_per_hour: u32,
    #[serde(default)]
    max_cost_per_day_cents: u32,
}

#[derive(Debug, Deserialize)]
struct CClawChannelsConfig {
    telegram: Option<CClawTelegramConfig>,
}

#[derive(Debug, Deserialize)]
struct CClawTelegramConfig {
    bot_token: String,
    #[serde(default)]
    allowed_users: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct CClawGatewayConfig {
    #[serde(default = "default_gateway_port")]
    port: u16,
    #[serde(default = "default_gateway_host")]
    host: String,
}

fn default_gateway_port() -> u16 {
    3000
}

fn default_gateway_host() -> String {
    "127.0.0.1".to_string()
}

/// Load configuration from CClaw's ~/.cclaw/config.json
/// Returns None if the file doesn't exist
pub fn load_cclaw_config() -> Result<Option<Config>> {
    let home = directories::UserDirs::new()
        .map(|u| u.home_dir().to_path_buf())
        .context("Could not find home directory")?;
    let cclaw_config_path = home.join(".cclaw/config.json");

    if !cclaw_config_path.exists() {
        return Ok(None);
    }

    let contents =
        std::fs::read_to_string(&cclaw_config_path).context("Failed to read ~/.cclaw/config.json")?;

    let cclaw_config: CClawJsonConfig =
        serde_json::from_str(&contents).context("Failed to parse ~/.cclaw/config.json")?;

    let mut config = Config::default();

    // Copy basic configuration
    config.api_key = cclaw_config.api_key;
    config.default_provider = cclaw_config.default_provider;
    config.default_model = cclaw_config.default_model;
    if let Some(temp) = cclaw_config.default_temperature {
        config.default_temperature = temp;
    }

    // Copy memory configuration
    if let Some(memory) = cclaw_config.memory {
        config.memory.backend = memory.backend;
    }

    // Copy autonomy configuration
    if let Some(autonomy) = cclaw_config.autonomy {
        config.autonomy.level = match autonomy.level {
            0 => AutonomyLevel::ReadOnly,
            1 => AutonomyLevel::Supervised,
            2 => AutonomyLevel::Full,
            _ => AutonomyLevel::Supervised,
        };
        config.autonomy.workspace_only = autonomy.workspace_only;
        if autonomy.max_actions_per_hour > 0 {
            config.autonomy.max_actions_per_hour = autonomy.max_actions_per_hour;
        }
        if autonomy.max_cost_per_day_cents > 0 {
            config.autonomy.max_cost_per_day_cents = autonomy.max_cost_per_day_cents;
        }
    }

    // Copy channels configuration
    if let Some(channels) = cclaw_config.channels {
        if let Some(telegram) = channels.telegram {
            config.channels_config.telegram = Some(TelegramConfig {
                bot_token: telegram.bot_token,
                allowed_users: telegram.allowed_users,
            });
        }
    }

    // Copy gateway configuration
    if let Some(gateway) = cclaw_config.gateway {
        config.gateway.port = gateway.port;
        config.gateway.host = gateway.host;
    }

    // Set paths to CClaw locations
    config.config_path = cclaw_config_path;
    config.workspace_dir = home.join(".cclaw");

    Ok(Some(config))
}

use super::traits::{Tool, ToolResult};
use crate::security::SecurityPolicy;
use async_trait::async_trait;
use serde_json::json;
use std::sync::Arc;

pub struct FileEditTool {
    security: Arc<SecurityPolicy>,
}

impl FileEditTool {
    pub fn new(security: Arc<SecurityPolicy>) -> Self {
        Self { security }
    }
}

#[async_trait]
impl Tool for FileEditTool {
    fn name(&self) -> &str {
        "file_edit"
    }

    fn description(&self) -> &str {
        "Edit a file: insert lines, delete lines, or replace content at specific line numbers"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        json!({
            "type": "object",
            "properties": {
                "path": {
                    "type": "string",
                    "description": "Relative path to the file within the workspace"
                },
                "operation": {
                    "type": "string",
                    "enum": ["insert", "delete", "replace"],
                    "description": "Operation to perform: insert (add lines), delete (remove lines), replace (substitute lines)"
                },
                "line": {
                    "type": "integer",
                    "description": "Line number to operate on (1-based). For insert: new line will be after this line. For delete: start line to delete. For replace: line to replace."
                },
                "content": {
                    "type": "string",
                    "description": "Content to insert or replace (not needed for delete)"
                },
                "end_line": {
                    "type": "integer",
                    "description": "End line for delete/replace operations (optional, only needed for range operations)"
                }
            },
            "required": ["path", "operation", "line"]
        })
    }

    async fn execute(&self, args: serde_json::Value) -> anyhow::Result<ToolResult> {
        let path = args
            .get("path")
            .and_then(|v| v.as_str())
            .ok_or_else(|| anyhow::anyhow!("Missing 'path' parameter"))?;

        let operation = args
            .get("operation")
            .and_then(|v| v.as_str())
            .ok_or_else(|| anyhow::anyhow!("Missing 'operation' parameter"))?;

        let line = args
            .get("line")
            .and_then(|v| v.as_i64())
            .ok_or_else(|| anyhow::anyhow!("Missing 'line' parameter"))? as usize;

        let content = args
            .get("content")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string());

        let end_line = args
            .get("end_line")
            .and_then(|v| v.as_i64())
            .map(|v| v as usize);

        // Security check: validate path is within workspace
        if !self.security.is_path_allowed(path) {
            return Ok(ToolResult {
                success: false,
                output: String::new(),
                error: Some(format!("Path not allowed by security policy: {path}")),
            });
        }

        let full_path = self.security.workspace_dir.join(path);

        // Check if file exists
        if !full_path.exists() {
            return Ok(ToolResult {
                success: false,
                output: String::new(),
                error: Some(format!("File not found: {path}")),
            });
        }

        // Read existing content
        let original_content = match tokio::fs::read_to_string(&full_path).await {
            Ok(c) => c,
            Err(e) => {
                return Ok(ToolResult {
                    success: false,
                    output: String::new(),
                    error: Some(format!("Failed to read file: {e}")),
                });
            }
        };

        let lines: Vec<&str> = original_content.lines().collect();
        let total_lines = lines.len();

        let new_content = match operation {
            "insert" => {
                let content = content.ok_or_else(|| anyhow::anyhow!("Missing 'content' for insert"))?;
                let insert_pos = line.saturating_sub(1).min(total_lines);
                
                let mut new_lines: Vec<String> = Vec::new();
                for (i, l) in lines.iter().enumerate() {
                    if i == insert_pos {
                        new_lines.push(content.clone());
                    }
                    new_lines.push(l.to_string());
                }
                // Handle case where inserting at the end
                if insert_pos >= total_lines {
                    new_lines.push(content);
                }
                new_lines.join("\n")
            }
            "delete" => {
                let end = end_line.unwrap_or(line).saturating_sub(1).min(total_lines.saturating_sub(1));
                let start = line.saturating_sub(1).min(end);
                
                if start >= total_lines {
                    return Ok(ToolResult {
                        success: false,
                        output: String::new(),
                        error: Some(format!("Line {} out of range (file has {} lines)", line, total_lines)),
                    });
                }
                
                let mut new_lines: Vec<String> = Vec::new();
                for (i, l) in lines.iter().enumerate() {
                    if i < start || i > end {
                        new_lines.push(l.to_string());
                    }
                }
                new_lines.join("\n")
            }
            "replace" => {
                let content = content.ok_or_else(|| anyhow::anyhow!("Missing 'content' for replace"))?;
                let replace_pos = line.saturating_sub(1).min(total_lines.saturating_sub(1));
                let end = end_line.unwrap_or(line).saturating_sub(1).min(total_lines.saturating_sub(1));
                
                if replace_pos >= total_lines {
                    return Ok(ToolResult {
                        success: false,
                        output: String::new(),
                        error: Some(format!("Line {} out of range (file has {} lines)", line, total_lines)),
                    });
                }
                
                let mut new_lines: Vec<String> = Vec::new();
                for (i, l) in lines.iter().enumerate() {
                    if i < replace_pos {
                        new_lines.push(l.to_string());
                    } else if i == replace_pos {
                        new_lines.push(content.clone());
                    } else if i > end {
                        new_lines.push(l.to_string());
                    }
                }
                new_lines.join("\n")
            }
            _ => {
                return Ok(ToolResult {
                    success: false,
                    output: String::new(),
                    error: Some(format!("Unknown operation: {}. Use 'insert', 'delete', or 'replace'", operation)),
                });
            }
        };

        // Write the modified content
        match tokio::fs::write(&full_path, &new_content).await {
            Ok(()) => {
                let lines_changed = match operation {
                    "insert" => 1,
                    "delete" => end_line.map(|e| e - line + 1).unwrap_or(1),
                    "replace" => end_line.map(|e| e - line + 1).unwrap_or(1),
                    _ => 0,
                };
                Ok(ToolResult {
                    success: true,
                    output: format!("{} operation completed. {} lines changed in {}", operation, lines_changed, path),
                    error: None,
                })
            }
            Err(e) => Ok(ToolResult {
                success: false,
                output: String::new(),
                error: Some(format!("Failed to write file: {e}")),
            }),
        }
    }
}

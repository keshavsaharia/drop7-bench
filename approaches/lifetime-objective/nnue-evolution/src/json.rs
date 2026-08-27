// Minimal JSON support, sufficient for the approach's own artifacts: the
// teacher corpus JSONL, evolution checkpoints and screen configs.  The
// writer side lives in game.rs (population artifacts); this is the reader.
// Only what the approach's own emitters produce: objects, arrays, strings,
// finite numbers, true/false/null.  Not a general-purpose parser.

#[derive(Clone, Debug, PartialEq)]
pub enum Json {
    Null,
    Bool(bool),
    Number(f64),
    String(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

impl Json {
    pub fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Object(entries) => entries.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    pub fn as_f64(&self) -> Option<f64> {
        match self {
            Json::Number(v) => Some(*v),
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            Json::String(s) => Some(s),
            _ => None,
        }
    }

    pub fn as_array(&self) -> Option<&[Json]> {
        match self {
            Json::Array(items) => Some(items),
            _ => None,
        }
    }
}

pub fn parse(text: &str) -> Result<Json, String> {
    let bytes = text.as_bytes();
    let mut cursor = 0;
    let value = parse_value(bytes, &mut cursor)?;
    skip_ws(bytes, &mut cursor);
    if cursor != bytes.len() {
        return Err(format!("trailing bytes at {cursor}"));
    }
    Ok(value)
}

fn skip_ws(bytes: &[u8], cursor: &mut usize) {
    while *cursor < bytes.len() && matches!(bytes[*cursor], b' ' | b'\t' | b'\n' | b'\r') {
        *cursor += 1;
    }
}

fn parse_value(bytes: &[u8], cursor: &mut usize) -> Result<Json, String> {
    skip_ws(bytes, cursor);
    if *cursor >= bytes.len() {
        return Err("unexpected end".into());
    }
    match bytes[*cursor] {
        b'{' => parse_object(bytes, cursor),
        b'[' => parse_array(bytes, cursor),
        b'"' => Ok(Json::String(parse_string(bytes, cursor)?)),
        b't' => literal(bytes, cursor, "true", Json::Bool(true)),
        b'f' => literal(bytes, cursor, "false", Json::Bool(false)),
        b'n' => literal(bytes, cursor, "null", Json::Null),
        _ => parse_number(bytes, cursor),
    }
}

fn literal(bytes: &[u8], cursor: &mut usize, text: &str, value: Json) -> Result<Json, String> {
    if bytes[*cursor..].starts_with(text.as_bytes()) {
        *cursor += text.len();
        Ok(value)
    } else {
        Err(format!("bad literal at {cursor}"))
    }
}

fn parse_object(bytes: &[u8], cursor: &mut usize) -> Result<Json, String> {
    *cursor += 1; // {
    let mut entries = Vec::new();
    skip_ws(bytes, cursor);
    if *cursor < bytes.len() && bytes[*cursor] == b'}' {
        *cursor += 1;
        return Ok(Json::Object(entries));
    }
    loop {
        skip_ws(bytes, cursor);
        let key = parse_string(bytes, cursor)?;
        skip_ws(bytes, cursor);
        if *cursor >= bytes.len() || bytes[*cursor] != b':' {
            return Err(format!("expected ':' at {cursor}"));
        }
        *cursor += 1;
        let value = parse_value(bytes, cursor)?;
        entries.push((key, value));
        skip_ws(bytes, cursor);
        match bytes.get(*cursor) {
            Some(b',') => *cursor += 1,
            Some(b'}') => {
                *cursor += 1;
                return Ok(Json::Object(entries));
            }
            _ => return Err(format!("expected ',' or '}}' at {cursor}")),
        }
    }
}

fn parse_array(bytes: &[u8], cursor: &mut usize) -> Result<Json, String> {
    *cursor += 1; // [
    let mut items = Vec::new();
    skip_ws(bytes, cursor);
    if *cursor < bytes.len() && bytes[*cursor] == b']' {
        *cursor += 1;
        return Ok(Json::Array(items));
    }
    loop {
        let value = parse_value(bytes, cursor)?;
        items.push(value);
        skip_ws(bytes, cursor);
        match bytes.get(*cursor) {
            Some(b',') => *cursor += 1,
            Some(b']') => {
                *cursor += 1;
                return Ok(Json::Array(items));
            }
            _ => return Err(format!("expected ',' or ']' at {cursor}")),
        }
    }
}

fn parse_string(bytes: &[u8], cursor: &mut usize) -> Result<String, String> {
    if bytes.get(*cursor) != Some(&b'"') {
        return Err(format!("expected string at {cursor}"));
    }
    *cursor += 1;
    let mut out = String::new();
    while *cursor < bytes.len() {
        match bytes[*cursor] {
            b'"' => {
                *cursor += 1;
                return Ok(out);
            }
            b'\\' => {
                *cursor += 1;
                let escaped = *bytes.get(*cursor).ok_or("bad escape")?;
                match escaped {
                    b'"' => out.push('"'),
                    b'\\' => out.push('\\'),
                    b'n' => out.push('\n'),
                    b't' => out.push('\t'),
                    b'r' => out.push('\r'),
                    _ => return Err("unsupported escape".into()),
                }
                *cursor += 1;
            }
            byte => {
                out.push(byte as char);
                *cursor += 1;
            }
        }
    }
    Err("unterminated string".into())
}

fn parse_number(bytes: &[u8], cursor: &mut usize) -> Result<Json, String> {
    let start = *cursor;
    while *cursor < bytes.len()
        && matches!(bytes[*cursor], b'0'..=b'9' | b'-' | b'+' | b'.' | b'e' | b'E')
    {
        *cursor += 1;
    }
    let text = std::str::from_utf8(&bytes[start..*cursor]).map_err(|_| "bad number")?;
    text.parse::<f64>()
        .map(Json::Number)
        .map_err(|e| format!("bad number '{text}': {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_a_root_record_shape() {
        let line = "{\"type\":\"root\",\"seed\":\"0xa52e0200\",\"move\":3,\"board\":[0,1,8],\"next\":4,\"movesRemaining\":5,\"columns\":[[3,123.5],[2,-9.25]],\"chosen\":3}";
        let value = parse(line).unwrap();
        assert_eq!(value.get("type").unwrap().as_str(), Some("root"));
        assert_eq!(value.get("move").unwrap().as_f64(), Some(3.0));
        let columns = value.get("columns").unwrap().as_array().unwrap();
        assert_eq!(columns.len(), 2);
        assert_eq!(columns[1].as_array().unwrap()[1].as_f64(), Some(-9.25));
    }
}

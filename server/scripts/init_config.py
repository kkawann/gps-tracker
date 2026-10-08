"""Create private development secrets without overwriting an existing .env."""
from pathlib import Path
import secrets


def main():
    target = Path(__file__).resolve().parents[1] / ".env"
    values = {
        "DB_PASSWORD": secrets.token_hex(24),
        "API_KEY": secrets.token_hex(32),
        "JWT_SECRET": secrets.token_hex(32),
        "MQTT_USERNAME": "",
        "MQTT_PASSWORD": "",
        "MQTT_BIND_ADDRESS": "127.0.0.1",
        "HTTP_BIND_ADDRESS": "127.0.0.1",
    }
    try:
        with target.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write("# Generated locally. Do not commit this file.\n")
            for key, value in values.items():
                stream.write(f"{key}={value}\n")
        try:
            target.chmod(0o600)
        except OSError:
            pass
    except FileExistsError:
        raise SystemExit(".env already exists; no settings were changed.")
    print("Created private .env with new random secrets and localhost bindings.")


if __name__ == "__main__":
    main()

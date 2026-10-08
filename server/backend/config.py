from pydantic_settings import BaseSettings
from functools import lru_cache


class Settings(BaseSettings):
    # Database
    DATABASE_URL: str

    # MQTT
    MQTT_BROKER: str = "mosquitto"
    MQTT_PORT: int = 1883
    MQTT_USERNAME: str = ""
    MQTT_PASSWORD: str = ""

    # Redis
    REDIS_URL: str = "redis://redis:6379"

    # API Security
    API_KEY: str
    JWT_SECRET: str
    JWT_ALGORITHM: str = "HS256"
    JWT_EXPIRE_MINUTES: int = 43200  # 30 days

    # OTP
    OTP_EXPIRE_MS: int = 300000  # 5 minutes (match ESP32)

    # App
    APP_NAME: str = "GPS Tracker Server"
    APP_VERSION: str = "2.1.0"
    DEBUG: bool = False

    # Speed limit (km/h) for alerts
    SPEED_LIMIT_KMH: float = 120.0

    class Config:
        env_file = ".env"


@lru_cache()
def get_settings():
    return Settings()

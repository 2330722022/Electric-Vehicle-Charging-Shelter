package com.example.onenet215;

public class WeatherInfo {
    public String weatherTimestamp;
    public double temperature;
    public double humidity;
    public double windSpeed;
    public double windDirection;
    public int weatherCode;
    public double todayMaxTemp;
    public double todayMinTemp;

    public String getWeatherIcon() {
        if (weatherCode <= 3) return "☀️";
        if (weatherCode <= 48) return "🌥️";
        if (weatherCode <= 67) return "🌧️";
        if (weatherCode <= 77) return "❄️";
        if (weatherCode <= 82) return "🌧️";
        return "🌩️";
    }

    public String getWindDirectionText() {
        if (windDirection >= 337.5 || windDirection < 22.5) return "北风";
        if (windDirection < 67.5) return "东北风";
        if (windDirection < 112.5) return "东风";
        if (windDirection < 157.5) return "东南风";
        if (windDirection < 202.5) return "南风";
        if (windDirection < 247.5) return "西南风";
        if (windDirection < 292.5) return "西风";
        return "西北风";
    }
}
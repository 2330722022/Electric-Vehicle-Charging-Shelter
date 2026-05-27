package com.example.onenet215;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.Response;

public class WeatherApiClient {
    private static final String TAG = "WeatherApi";
    private static final String BASE_URL = "https://api.open-meteo.com/v1/forecast";

    private final OkHttpClient client;
    private final Handler mainHandler;

    public interface WeatherCallback {
        void onSuccess(WeatherInfo info);
        void onError(String error);
    }

    public WeatherApiClient() {
        this.client = new OkHttpClient.Builder()
                .connectTimeout(10, TimeUnit.SECONDS)
                .readTimeout(15, TimeUnit.SECONDS)
                .build();
        this.mainHandler = new Handler(Looper.getMainLooper());
    }

    public void fetchWeather(double lat, double lon, WeatherCallback callback) {
        String url = BASE_URL
                + "?latitude=" + lat + "&longitude=" + lon
                + "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,wind_direction_10m,weather_code"
                + "&daily=temperature_2m_max,temperature_2m_min&timezone=Asia%2FShanghai&forecast_days=1";

        Log.d(TAG, "请求天气: lat=" + lat + " lon=" + lon);

        Request request = new Request.Builder()
                .url(url)
                .get()
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                Log.e(TAG, "天气请求失败", e);
                mainHandler.post(() -> callback.onError("天气获取失败: " + e.getMessage()));
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (!response.isSuccessful()) {
                    mainHandler.post(() -> callback.onError("天气服务异常 (HTTP " + response.code() + ")"));
                    response.close();
                    return;
                }

                try {
                    String body = response.body().string();
                    Log.d(TAG, "天气响应: " + body);
                    JSONObject json = new JSONObject(body);
                    JSONObject current = json.getJSONObject("current");

                    WeatherInfo info = new WeatherInfo();
                    info.weatherTimestamp = current.getString("time");
                    info.temperature = current.getDouble("temperature_2m");
                    info.humidity = current.getDouble("relative_humidity_2m");
                    info.windSpeed = current.getDouble("wind_speed_10m");
                    info.windDirection = current.getDouble("wind_direction_10m");
                    info.weatherCode = current.optInt("weather_code", 0);

                    if (json.has("daily")) {
                        JSONObject daily = json.getJSONObject("daily");
                        info.todayMaxTemp = daily.getJSONArray("temperature_2m_max").getDouble(0);
                        info.todayMinTemp = daily.getJSONArray("temperature_2m_min").getDouble(0);
                    }

                    mainHandler.post(() -> callback.onSuccess(info));

                } catch (JSONException e) {
                    Log.e(TAG, "解析天气响应失败", e);
                    mainHandler.post(() -> callback.onError("解析天气数据失败"));
                } finally {
                    response.close();
                }
            }
        });
    }
}

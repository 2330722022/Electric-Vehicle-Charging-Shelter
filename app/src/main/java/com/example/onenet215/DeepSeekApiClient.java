package com.example.onenet215;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.IOException;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class DeepSeekApiClient {
    private static final String TAG = "DeepSeekApi";
    private static final String API_URL = "https://api.deepseek.com/v1/chat/completions";
    private static final String API_KEY = BuildConfig.DEEPSEEK_API_KEY;
    private static final MediaType JSON = MediaType.get("application/json; charset=utf-8");

    private final OkHttpClient client;
    private final Handler mainHandler;

    public interface DiagnoseCallback {
        void onSuccess(String reply);
        void onError(String error);
    }

    public DeepSeekApiClient() {
        this.client = new OkHttpClient.Builder()
                .connectTimeout(15, TimeUnit.SECONDS)
                .readTimeout(30, TimeUnit.SECONDS)
                .writeTimeout(15, TimeUnit.SECONDS)
                .build();
        this.mainHandler = new Handler(Looper.getMainLooper());
    }

    public void chat(String userMessage, DiagnoseCallback callback) {
        chatWithMode(userMessage, false, callback);
    }

    public void chatWithMode(String userMessage, boolean isProfessional, DiagnoseCallback callback) {
        String systemPrompt;
        int maxTokens;
        double temperature;
        if (isProfessional) {
            systemPrompt = "你是一个专业的充电棚消防安全专家助手。请简洁专业地解答用户的消防安全相关问题，"
                    + "回答控制在80字以内，直接给出答案。";
            maxTokens = 200;
            temperature = 0.6;
        } else {
            systemPrompt = "你是DeepSeek，一个乐于助人的AI助手。请用中文回答用户的问题，"
                    + "提供有帮助、准确、详细的回答。";
            maxTokens = 600;
            temperature = 0.8;
        }

        try {
            JSONObject body = new JSONObject();
            body.put("model", "deepseek-chat");
            body.put("max_tokens", maxTokens);
            body.put("temperature", temperature);

            JSONArray messages = new JSONArray();

            JSONObject sysMsg = new JSONObject();
            sysMsg.put("role", "system");
            sysMsg.put("content", systemPrompt);
            messages.put(sysMsg);

            JSONObject userMsg = new JSONObject();
            userMsg.put("role", "user");
            userMsg.put("content", userMessage);
            messages.put(userMsg);

            body.put("messages", messages);

            Log.d(TAG, "发送对话请求: " + userMessage);

            RequestBody requestBody = RequestBody.create(body.toString(), JSON);
            Request request = new Request.Builder()
                    .url(API_URL)
                    .header("Authorization", "Bearer " + API_KEY)
                    .header("Content-Type", "application/json")
                    .post(requestBody)
                    .build();

            client.newCall(request).enqueue(new Callback() {
                @Override
                public void onFailure(Call call, IOException e) {
                    Log.e(TAG, "DeepSeek请求失败", e);
                    mainHandler.post(() -> callback.onError("网络请求失败: " + e.getMessage()));
                }

                @Override
                public void onResponse(Call call, Response response) throws IOException {
                    if (!response.isSuccessful()) {
                        String errBody = response.body() != null ? response.body().string() : "";
                        Log.e(TAG, "DeepSeek响应错误: " + response.code() + " " + errBody);
                        mainHandler.post(() -> callback.onError("AI服务响应异常 (HTTP " + response.code() + ")"));
                        response.close();
                        return;
                    }

                    try {
                        String respBody = response.body().string();
                        Log.d(TAG, "DeepSeek响应: " + respBody);
                        JSONObject jsonResp = new JSONObject(respBody);
                        JSONArray choices = jsonResp.getJSONArray("choices");
                        JSONObject firstChoice = choices.getJSONObject(0);
                        JSONObject message = firstChoice.getJSONObject("message");
                        String content = message.getString("content").trim();

                        mainHandler.post(() -> callback.onSuccess(content));
                    } catch (JSONException e) {
                        Log.e(TAG, "解析DeepSeek响应失败", e);
                        mainHandler.post(() -> callback.onError("解析AI回复失败"));
                    } finally {
                        response.close();
                    }
                }
            });

        } catch (JSONException e) {
            Log.e(TAG, "构建请求JSON失败", e);
            mainHandler.post(() -> callback.onError("构建请求失败"));
        }
    }

    public void diagnose(SensorSnapshot snapshot, DiagnoseCallback callback) {
        String systemPrompt = "你是一个专业的充电棚消防安全专家。请根据提供的实时传感器数据，"
                + "在40字以内给出一句精炼的消防安全诊断和逃生指引。直接给出建议，不需要额外解释。";

        String userPrompt = "当前充电棚实时数据：温度" + snapshot.temperature + "°C，"
                + "湿度" + snapshot.humidity + "%，"
                + "光照" + snapshot.light + "Lux，"
                + "PM2.5为" + snapshot.pm25 + "μg/m³，"
                + "烟雾MQ-2值" + snapshot.smoke + "，"
                + "火焰传感器值" + snapshot.flame + "，"
                + "振动状态" + snapshot.vibration + "，"
                + "舵机角度" + snapshot.tiltAngle + "°，"
                + "风扇状态" + snapshot.fanStatus + "，"
                + "综合告警码" + snapshot.alarmState + "（0=正常），"
                + "LED=" + (snapshot.ledOn ? "开" : "关") + "，"
                + "蜂鸣器=" + (snapshot.beepOn ? "开" : "关") + "。"
                + "请在40字内给出一句话消防安全诊断和逃生指引：";

        try {
            JSONObject body = new JSONObject();
            body.put("model", "deepseek-chat");
            body.put("max_tokens", 120);
            body.put("temperature", 0.3);

            JSONArray messages = new JSONArray();

            JSONObject sysMsg = new JSONObject();
            sysMsg.put("role", "system");
            sysMsg.put("content", systemPrompt);
            messages.put(sysMsg);

            JSONObject userMsg = new JSONObject();
            userMsg.put("role", "user");
            userMsg.put("content", userPrompt);
            messages.put(userMsg);

            body.put("messages", messages);

            Log.d(TAG, "发送诊断请求: " + userPrompt);

            RequestBody requestBody = RequestBody.create(body.toString(), JSON);
            Request request = new Request.Builder()
                    .url(API_URL)
                    .header("Authorization", "Bearer " + API_KEY)
                    .header("Content-Type", "application/json")
                    .post(requestBody)
                    .build();

            client.newCall(request).enqueue(new Callback() {
                @Override
                public void onFailure(Call call, IOException e) {
                    Log.e(TAG, "DeepSeek请求失败", e);
                    mainHandler.post(() -> callback.onError("网络请求失败: " + e.getMessage()));
                }

                @Override
                public void onResponse(Call call, Response response) throws IOException {
                    if (!response.isSuccessful()) {
                        String errBody = response.body() != null ? response.body().string() : "";
                        Log.e(TAG, "DeepSeek响应错误: " + response.code() + " " + errBody);
                        mainHandler.post(() -> callback.onError("AI服务响应异常 (HTTP " + response.code() + ")"));
                        response.close();
                        return;
                    }

                    try {
                        String respBody = response.body().string();
                        Log.d(TAG, "DeepSeek响应: " + respBody);
                        JSONObject jsonResp = new JSONObject(respBody);
                        JSONArray choices = jsonResp.getJSONArray("choices");
                        JSONObject firstChoice = choices.getJSONObject(0);
                        JSONObject message = firstChoice.getJSONObject("message");
                        String content = message.getString("content").trim();

                        mainHandler.post(() -> callback.onSuccess(content));
                    } catch (JSONException e) {
                        Log.e(TAG, "解析DeepSeek响应失败", e);
                        mainHandler.post(() -> callback.onError("解析AI回复失败"));
                    } finally {
                        response.close();
                    }
                }
            });

        } catch (JSONException e) {
            Log.e(TAG, "构建请求JSON失败", e);
            mainHandler.post(() -> callback.onError("构建请求失败"));
        }
    }
}
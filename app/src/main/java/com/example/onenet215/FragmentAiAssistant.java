package com.example.onenet215;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class FragmentAiAssistant extends Fragment {
    private static final String TAG = "FragmentAiAssistant";

    private LinearLayout llChatContainer;
    private ScrollView scrollChat;
    private Button btnAiDiagnose;
    private Button btnChatSend;
    private EditText etChatInput;
    private Button btnLowerThreshold;
    private com.google.android.material.button.MaterialButton btnModeToggle;
    private TextView tvAiStatus;
    private TextView tvModeHint;
    private TextView tvWeatherAlert;
    private LinearLayout llWeatherAlert;
    private boolean isProfessionalMode = true;

    private DeepSeekApiClient deepSeekClient;
    private WeatherApiClient weatherClient;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    private ThresholdLowerCallback thresholdLowerCallback;
    private SensorSnapshotProvider snapshotProvider;

    public interface SensorSnapshotProvider {
        SensorSnapshot getLatestSnapshot();
    }

    public interface ThresholdLowerCallback {
        void onLowerThreshold(int newThreshold);
    }

    public void setThresholdLowerCallback(ThresholdLowerCallback callback) {
        this.thresholdLowerCallback = callback;
    }

    public void setSnapshotProvider(SensorSnapshotProvider provider) {
        this.snapshotProvider = provider;
    }

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_ai_assistant, container, false);

        llChatContainer = view.findViewById(R.id.llChatContainer);
        scrollChat = view.findViewById(R.id.scrollChat);
        btnAiDiagnose = view.findViewById(R.id.btnAiDiagnose);
        btnChatSend = view.findViewById(R.id.btnChatSend);
        etChatInput = view.findViewById(R.id.etChatInput);
        btnLowerThreshold = view.findViewById(R.id.btnLowerThreshold);
        btnModeToggle = view.findViewById(R.id.btnModeToggle);
        tvModeHint = view.findViewById(R.id.tvModeHint);
        tvAiStatus = view.findViewById(R.id.tvAiStatus);
        tvWeatherAlert = view.findViewById(R.id.tvWeatherAlert);
        llWeatherAlert = view.findViewById(R.id.llWeatherAlert);

        deepSeekClient = new DeepSeekApiClient();
        weatherClient = new WeatherApiClient();

        btnAiDiagnose.setOnClickListener(v -> triggerDiagnose());

        btnChatSend.setOnClickListener(v -> sendChatMessage());

        etChatInput.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND) {
                sendChatMessage();
                return true;
            }
            return false;
        });

        btnLowerThreshold.setOnClickListener(v -> {
            if (thresholdLowerCallback != null) {
                thresholdLowerCallback.onLowerThreshold(45);
                addSystemMessage("✅ 已一键将温度告警阈值调低至 45°C");
                llWeatherAlert.setVisibility(View.GONE);
                ToastUtils.show(getActivity(), "温度阈值已调低至 45°C");
            }
        });

        btnModeToggle.setOnClickListener(v -> {
            isProfessionalMode = !isProfessionalMode;
            updateModeUI();
            String modeMsg = isProfessionalMode
                    ? "🔄 已切换至专业模式，仅回答消防安全相关问题"
                    : "🔄 已切换至普通模式，可自由对话各类话题";
            addSystemMessage(modeMsg);
        });

        addWelcomeMessage();

        fetchWeather();

        return view;
    }

    private void sendChatMessage() {
        String input = etChatInput.getText().toString().trim();
        if (TextUtils.isEmpty(input)) return;

        etChatInput.setText("");

        InputMethodManager imm = (InputMethodManager) requireActivity()
                .getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null) {
            imm.hideSoftInputFromWindow(etChatInput.getWindowToken(), 0);
        }

        addUserBubble(input);

        btnChatSend.setEnabled(false);
        tvAiStatus.setText("思考中");
        tvAiStatus.setTextColor(0xFFFF9800);
        addTypingIndicator();

        deepSeekClient.chatWithMode(input, isProfessionalMode, new DeepSeekApiClient.DiagnoseCallback() {
            @Override
            public void onSuccess(String reply) {
                mainHandler.post(() -> {
                    if (!isAdded()) return;
                    removeTypingIndicator();
                    addAiBubble(reply);
                    btnChatSend.setEnabled(true);
                    tvAiStatus.setText("就绪");
                    tvAiStatus.setTextColor(0xFF4CAF50);
                });
            }

            @Override
            public void onError(String error) {
                mainHandler.post(() -> {
                    if (!isAdded()) return;
                    removeTypingIndicator();
                    addSystemMessage("❌ " + error);
                    btnChatSend.setEnabled(true);
                    tvAiStatus.setText("错误");
                    tvAiStatus.setTextColor(0xFFFF5722);
                });
            }
        });
    }

    private void addWelcomeMessage() {
        String welcome = "👋 你好！我是充电棚AI消防安全助手。\n\n"
                + "• 点击「⚡AI一键诊断」获取实时安全评估\n"
                + "• 在下方输入框可以与我自由对话";
        addAiBubble(welcome);
    }

    private void addAiBubble(String text) {
        Context ctx = getContext();
        if (ctx == null || llChatContainer == null) return;

        LinearLayout wrapper = new LinearLayout(ctx);
        wrapper.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams wrapParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        wrapParams.setMargins(0, dpToPx(4), dpToPx(40), dpToPx(4));
        wrapper.setLayoutParams(wrapParams);
        wrapper.setGravity(Gravity.START);

        TextView avatar = new TextView(ctx);
        avatar.setText("🤖");
        avatar.setTextSize(16);
        LinearLayout.LayoutParams avatarParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        avatarParams.gravity = Gravity.TOP;
        avatarParams.setMargins(0, dpToPx(4), dpToPx(6), 0);
        avatar.setLayoutParams(avatarParams);
        wrapper.addView(avatar);

        TextView tv = new TextView(ctx);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tv.setLayoutParams(params);
        tv.setText(text);
        tv.setTextSize(13);
        tv.setTextColor(0xFF37474F);
        tv.setLineSpacing(dpToPx(3), 1f);
        tv.setPadding(dpToPx(14), dpToPx(10), dpToPx(14), dpToPx(10));
        tv.setMaxWidth((int) (ctx.getResources().getDisplayMetrics().widthPixels * 0.72f));

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFFFFFFFF);
        float[] corners = new float[]{dpToPx(4), dpToPx(4), dpToPx(14), dpToPx(14), dpToPx(14), dpToPx(14), dpToPx(4), dpToPx(4)};
        bg.setCornerRadii(corners);
        tv.setBackground(bg);
        tv.setElevation(dpToPx(2));

        wrapper.addView(tv);
        llChatContainer.addView(wrapper);
        scrollToBottom();
    }

    private void addUserBubble(String text) {
        Context ctx = getContext();
        if (ctx == null || llChatContainer == null) return;

        LinearLayout wrapper = new LinearLayout(ctx);
        wrapper.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams wrapParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        wrapParams.setMargins(dpToPx(40), dpToPx(4), 0, dpToPx(4));
        wrapper.setLayoutParams(wrapParams);
        wrapper.setGravity(Gravity.END);

        TextView tv = new TextView(ctx);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        tv.setLayoutParams(params);
        tv.setText(text);
        tv.setTextSize(13);
        tv.setTextColor(0xFF0D47A1);
        tv.setLineSpacing(dpToPx(3), 1f);
        tv.setPadding(dpToPx(14), dpToPx(10), dpToPx(14), dpToPx(10));
        tv.setMaxWidth((int) (ctx.getResources().getDisplayMetrics().widthPixels * 0.72f));

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFFE3F2FD);
        float[] corners = new float[]{dpToPx(14), dpToPx(14), dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(4), dpToPx(14), dpToPx(14)};
        bg.setCornerRadii(corners);
        tv.setBackground(bg);
        tv.setElevation(dpToPx(2));

        wrapper.addView(tv);

        TextView avatar = new TextView(ctx);
        avatar.setText("👤");
        avatar.setTextSize(16);
        LinearLayout.LayoutParams avatarParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        avatarParams.gravity = Gravity.TOP;
        avatarParams.setMargins(dpToPx(6), dpToPx(4), 0, 0);
        avatar.setLayoutParams(avatarParams);
        wrapper.addView(avatar);

        llChatContainer.addView(wrapper);
        scrollToBottom();
    }

    private void addSystemMessage(String text) {
        Context ctx = getContext();
        if (ctx == null || llChatContainer == null) return;

        TextView tv = new TextView(ctx);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.setMargins(0, dpToPx(5), 0, dpToPx(5));
        params.gravity = Gravity.CENTER;
        tv.setLayoutParams(params);
        tv.setText(text);
        tv.setTextSize(11);
        tv.setTextColor(0xFF757575);
        tv.setPadding(dpToPx(12), dpToPx(6), dpToPx(12), dpToPx(6));
        tv.setGravity(Gravity.CENTER);

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFFF5F5F5);
        bg.setCornerRadius(dpToPx(8));
        tv.setBackground(bg);

        llChatContainer.addView(tv);
        scrollToBottom();
    }

    private void addTypingIndicator() {
        Context ctx = getContext();
        if (ctx == null || llChatContainer == null) return;

        TextView tv = new TextView(ctx);
        tv.setTag("typing");
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        params.setMargins(0, dpToPx(5), dpToPx(48), dpToPx(5));
        params.gravity = Gravity.START;
        tv.setLayoutParams(params);
        tv.setText("🤔 AI 分析中...");
        tv.setTextSize(13);
        tv.setTextColor(0xFF1A237E);
        tv.setPadding(dpToPx(16), dpToPx(14), dpToPx(16), dpToPx(14));

        GradientDrawable bg = new GradientDrawable();
        bg.setColor(0xFFFFFFFF);
        bg.setCornerRadius(dpToPx(12));
        tv.setBackground(bg);
        tv.setElevation(dpToPx(2));

        llChatContainer.addView(tv);
        scrollToBottom();
    }

    private void removeTypingIndicator() {
        if (llChatContainer == null) return;
        for (int i = llChatContainer.getChildCount() - 1; i >= 0; i--) {
            View child = llChatContainer.getChildAt(i);
            if ("typing".equals(child.getTag())) {
                llChatContainer.removeView(child);
                break;
            }
        }
    }

    private void scrollToBottom() {
        if (scrollChat != null) {
            scrollChat.post(() -> scrollChat.fullScroll(View.FOCUS_DOWN));
        }
    }

    private void triggerDiagnose() {
        if (snapshotProvider == null) {
            addSystemMessage("⚠️ 无法获取传感器数据，请稍后重试");
            return;
        }

        SensorSnapshot snapshot = snapshotProvider.getLatestSnapshot();
        if (!snapshot.isOnline) {
            addSystemMessage("⚠️ 设备离线，无法获取实时数据，诊断结果可能不准确");
        }

        String timeStamp = new SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(new Date());
        String userMsg = "📊 [" + timeStamp + "] 请求AI安全诊断\n"
                + "温度:" + snapshot.temperature + "°C | 烟雾:" + snapshot.smoke
                + " | 火焰:" + snapshot.flame + " | 告警码:" + snapshot.alarmState;
        addUserBubble(userMsg);

        btnAiDiagnose.setEnabled(false);
        btnAiDiagnose.setText("⏳ 诊断中...");
        tvAiStatus.setText("分析中");
        tvAiStatus.setTextColor(0xFFFF9800);
        addTypingIndicator();

        deepSeekClient.diagnose(snapshot, new DeepSeekApiClient.DiagnoseCallback() {
            @Override
            public void onSuccess(String reply) {
                mainHandler.post(() -> {
                    if (!isAdded()) return;
                    removeTypingIndicator();
                    addAiBubble("🛡️ " + reply);
                    btnAiDiagnose.setEnabled(true);
                    btnAiDiagnose.setText("⚡ AI 一键诊断当前车棚安全");
                    tvAiStatus.setText("就绪");
                    tvAiStatus.setTextColor(0xFF4CAF50);
                });
            }

            @Override
            public void onError(String error) {
                mainHandler.post(() -> {
                    if (!isAdded()) return;
                    removeTypingIndicator();
                    addSystemMessage("❌ " + error);
                    btnAiDiagnose.setEnabled(true);
                    btnAiDiagnose.setText("⚡ AI 一键诊断当前车棚安全");
                    tvAiStatus.setText("错误");
                    tvAiStatus.setTextColor(0xFFFF5722);
                });
            }
        });
    }

    private void fetchWeather() {
        if (weatherClient == null) return;
        weatherClient.fetchWeather(22.5431, 114.0579, new WeatherApiClient.WeatherCallback() {
            @Override
            public void onSuccess(WeatherInfo info) {
                mainHandler.post(() -> {
                    if (!isAdded()) return;
                    handleWeatherUpdate(info);
                });
            }

            @Override
            public void onError(String error) {
                Log.w(TAG, "天气获取失败: " + error);
            }
        });
    }

    private void handleWeatherUpdate(WeatherInfo info) {
        addSystemMessage(info.getWeatherIcon() + " 本地天气 | "
                + String.format(Locale.getDefault(), "%.0f°C", info.temperature)
                + " | 湿度" + String.format(Locale.getDefault(), "%.0f%%", info.humidity)
                + " | " + info.getWindDirectionText()
                + String.format(Locale.getDefault(), " %.0f km/h", info.windSpeed)
                + " | 今日最高" + String.format(Locale.getDefault(), "%.0f°C", info.todayMaxTemp));

        if (info.todayMaxTemp > 35.0 && tvWeatherAlert != null && llWeatherAlert != null) {
            String alertText = "🌡️ 高温环境预警：今日预报最高气温 "
                    + String.format(Locale.getDefault(), "%.0f°C", info.todayMaxTemp)
                    + "，超过35°C安全线。建议将网关硬件温度告警阈值调低至 45°C，以防电池热失控误报。";
            tvWeatherAlert.setText(alertText);
            llWeatherAlert.setVisibility(View.VISIBLE);
        }
    }

    private void updateModeUI() {
        if (btnModeToggle == null || tvModeHint == null) return;
        if (isProfessionalMode) {
            btnModeToggle.setText("专业模式 🔒");
            btnModeToggle.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF1A237E));
            btnModeToggle.setTextColor(0xFFFFFFFF);
            tvModeHint.setText("当前仅回答消防相关问题");
        } else {
            btnModeToggle.setText("普通模式 🌐");
            btnModeToggle.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFFE3F2FD));
            btnModeToggle.setTextColor(0xFF0D47A1);
            tvModeHint.setText("可自由对话各类话题");
        }
    }

    private int dpToPx(int dp) {
        return (int) (dp * getResources().getDisplayMetrics().density + 0.5f);
    }
}
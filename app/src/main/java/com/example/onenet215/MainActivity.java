package com.example.onenet215;

import android.Manifest;
import android.app.AlertDialog;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.StrictMode;
import android.provider.Settings;
import android.util.Log;
import android.view.View;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.fragment.app.Fragment;
import androidx.viewpager2.widget.ViewPager2;

import com.google.android.material.bottomnavigation.BottomNavigationView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity
        implements FragmentAiAssistant.SensorSnapshotProvider {

    private static final String TAG = "MainActivity";
    private static final String CHANNEL_ID = "fire_alarm_channel";
    private static final int ALARM_NOTIFICATION_ID = 1001;

    private DataFetcher dataFetcher;

    String humidity_value, light_value, temperature_value;
    String pm2_5_value, mq2_value, flame_value, vibration_value;
    String tilt_angle_value = "--";
    volatile int alarm_state = 0;
    volatile int lastNotifiedAlarmState = 0;
    String alarm_type = "";
    volatile boolean led_state = false;
    volatile boolean beep_state = false;
    volatile boolean fan_state = false;
    volatile String fan_status = "--";
    volatile boolean work_mode_auto = true;
    volatile int temp_threshold = 25;
    volatile int smoke_threshold = 1500;
    volatile int pm25_threshold = 200;
    volatile int steering_angle = 90;
    volatile boolean isDeviceOnline = false;
    volatile long lastDataReceiveTime = 0;
    boolean isThresholdCommandPending = false;
    boolean isSmokeThresholdPending = false;
    boolean isPm25ThresholdPending = false;
    boolean isSteeringCommandPending = false;
    int consecutiveFailures = 0;
    static final int MAX_FAILURES_BEFORE_OFFLINE = 3;

    String gatewayIp = null;

    private long lastRoomWriteTime = 0;
    private int lastWrittenAlarmState = -1;
    private final java.util.concurrent.ExecutorService roomWriteExecutor = Executors.newSingleThreadExecutor();
    private static final long ROOM_WRITE_INTERVAL_MS = 15_000;

    Handler timeoutHandler = new Handler(Looper.getMainLooper());
    Runnable thresholdTimeoutRunnable = null;
    static final int COMMAND_TIMEOUT_MS = 3000;

    private ViewPager2 viewPager;
    private BottomNavigationView bottomNav;
    private ImageView ivAlarmIndicator;
    private TextView tvConnectionStatus;

    private FragmentSensor fragmentSensor;
    private FragmentControl fragmentControl;
    private FragmentStatus fragmentStatus;
    private FragmentAiAssistant fragmentAiAssistant;
    private final List<Fragment> fragmentList = new ArrayList<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (!getSharedPreferences("auth_prefs", MODE_PRIVATE).getBoolean("is_logged_in", false)) {
            startActivity(new Intent(this, LoginActivity.class));
            finish();
            return;
        }

        setContentView(R.layout.activity_main);

        createNotificationChannel();
        checkAndRequestNotificationPermission();

        if (android.os.Build.VERSION.SDK_INT > 9) {
            StrictMode.ThreadPolicy policy = new StrictMode.ThreadPolicy.Builder().permitAll().build();
            StrictMode.setThreadPolicy(policy);
        }

        ivAlarmIndicator = findViewById(R.id.ivAlarmIndicator);
        tvConnectionStatus = findViewById(R.id.tvConnectionStatus);
        tvConnectionStatus.setOnClickListener(v -> {
            if (!isDeviceOnline) {
                manualReconnect();
            }
        });

        tvConnectionStatus.setOnLongClickListener(v -> {
            getSharedPreferences("auth_prefs", MODE_PRIVATE)
                    .edit().putBoolean("is_logged_in", false).apply();
            startActivity(new Intent(this, LoginActivity.class));
            finish();
            return true;
        });

        initFragments();
        initViewPager();
        initBottomNav();

        initDataFetcher();
    }

    private void initFragments() {
        fragmentSensor = new FragmentSensor();
        fragmentSensor.setControlCallback((property, value) -> controlDevice(property, value));
        fragmentControl = new FragmentControl();
        fragmentStatus = new FragmentStatus();
        fragmentAiAssistant = new FragmentAiAssistant();
        fragmentAiAssistant.setSnapshotProvider(this);
        fragmentAiAssistant.setThresholdLowerCallback(newThreshold -> {
            controlDevice("temp_threshold", newThreshold);
        });
        fragmentList.add(fragmentSensor);
        fragmentList.add(fragmentControl);
        fragmentList.add(fragmentStatus);
        fragmentList.add(fragmentAiAssistant);
    }

    private void initViewPager() {
        viewPager = findViewById(R.id.viewPager);
        viewPager.setAdapter(new FragmentAdapter(this, fragmentList));
        viewPager.setUserInputEnabled(true);

        viewPager.setPageTransformer(new ViewPager2.PageTransformer() {
            @Override
            public void transformPage(@NonNull View page, float position) {
                float absPos = Math.abs(position);
                page.setAlpha(1f - 0.25f * absPos);
                page.setScaleX(0.95f + 0.05f * (1f - absPos));
                page.setScaleY(0.95f + 0.05f * (1f - absPos));
            }
        });

        viewPager.registerOnPageChangeCallback(new ViewPager2.OnPageChangeCallback() {
            @Override
            public void onPageSelected(int position) {
                bottomNav.getMenu().getItem(position).setChecked(true);
            }
        });
    }

    private void initBottomNav() {
        bottomNav = findViewById(R.id.bottomNav);
        bottomNav.setOnNavigationItemSelectedListener(item -> {
            int id = item.getItemId();
            if (id == R.id.nav_sensor) {
                viewPager.setCurrentItem(0, true);
            } else if (id == R.id.nav_control) {
                viewPager.setCurrentItem(1, true);
            } else if (id == R.id.nav_status) {
                viewPager.setCurrentItem(2, true);
            } else if (id == R.id.nav_ai) {
                viewPager.setCurrentItem(3, true);
            }
            return true;
        });
    }

    private void notifyAllFragments() {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            fragmentSensor.updateSensorData(temperature_value, humidity_value, light_value,
                    pm2_5_value, mq2_value, flame_value);
            fragmentSensor.updateAlarmState(alarm_state, temp_threshold, pm25_threshold);
            fragmentSensor.setWorkMode(work_mode_auto);
            fragmentSensor.refreshUI(isDeviceOnline);

            fragmentControl.refreshUI(isDeviceOnline);

            String fanStatusText = (!fan_status.equals("--") && !fan_status.isEmpty()) ? fan_status : (fan_state ? "运行中" : "已停止");
            fragmentStatus.updateStatusData(fanStatusText, String.valueOf(steering_angle));
            fragmentStatus.updateAlarmState(alarm_state, temp_threshold, pm25_threshold);
            fragmentStatus.updateConnectionStatus(isDeviceOnline);
            fragmentStatus.refreshUI(isDeviceOnline);

            updateTopBar();
            updateLastTimeText();
        });
    }

    private void updateTopBar() {
        if (alarm_state == 0) {
            ivAlarmIndicator.setImageResource(R.drawable.ic_circle_green);
        } else {
            ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
        }
        if (isDeviceOnline) {
            tvConnectionStatus.setText("已连接");
            tvConnectionStatus.setTextColor(0xFFFFFFFF);
        } else {
            tvConnectionStatus.setText("点击重连");
            tvConnectionStatus.setTextColor(0xFFFFCDD2);
        }
    }

    private void manualReconnect() {
        Toast.makeText(this, "正在重新连接...", Toast.LENGTH_SHORT).show();
        consecutiveFailures = 0;
        isDeviceOnline = false;

        if (dataFetcher != null) {
            dataFetcher.startPolling();
        }

        Log.d(TAG, "手动重连已触发");
    }

    private void updateLastTimeText() {
        String timeText;
        if (lastDataReceiveTime > 0) {
            SimpleDateFormat sdf = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault());
            timeText = "最后更新：" + sdf.format(new Date(lastDataReceiveTime));
        } else {
            timeText = "最后更新：--";
        }
        fragmentSensor.updateLastTime(timeText);
        fragmentStatus.updateLastTime(timeText);
    }

    public void requestRefresh() {
        lastDataReceiveTime = System.currentTimeMillis();
        notifyAllFragments();
        Toast.makeText(this, "数据已刷新", Toast.LENGTH_SHORT).show();
    }

    @Override
    public synchronized SensorSnapshot getLatestSnapshot() {
        SensorSnapshot snapshot = new SensorSnapshot();
        snapshot.temperature = temperature_value != null ? temperature_value : "--";
        snapshot.humidity = humidity_value != null ? humidity_value : "--";
        snapshot.light = light_value != null ? light_value : "--";
        snapshot.pm25 = pm2_5_value != null ? pm2_5_value : "--";
        snapshot.smoke = mq2_value != null ? mq2_value : "--";
        snapshot.flame = flame_value != null ? flame_value : "--";
        snapshot.vibration = vibration_value != null ? vibration_value : "--";
        snapshot.tiltAngle = tilt_angle_value;
        snapshot.fanStatus = (!fan_status.equals("--") && !fan_status.isEmpty()) ? fan_status : (fan_state ? "运行中" : "已停止");
        snapshot.alarmState = alarm_state;
        snapshot.ledOn = led_state;
        snapshot.beepOn = beep_state;
        snapshot.fanOn = fan_state;
        snapshot.isOnline = isDeviceOnline;
        snapshot.timestamp = lastDataReceiveTime;
        return snapshot;
    }

    private void initDataFetcher() {
        dataFetcher = new DataFetcher(this);

        dataFetcher.setDataCallback(new DataFetcher.DataCallback() {
            @Override
            public void onDataReceived(JSONObject data) {
                Log.d(TAG, "[HTTP] 轮询收到数据: " + data.toString());
                consecutiveFailures = 0;
                isDeviceOnline = true;
                lastDataReceiveTime = System.currentTimeMillis();
                try {
                    JSONArray parsedData = DataFetcher.parseResponse(data);
                    parseDeviceData(parsedData, "HTTP");
                } catch (JSONException e) {
                    Log.e(TAG, "[HTTP] 解析数据失败", e);
                }
            }

            @Override
            public void onError(String error) {
                Log.w(TAG, "HTTP请求失败: " + error);
                consecutiveFailures++;
                if (consecutiveFailures >= MAX_FAILURES_BEFORE_OFFLINE) {
                    if (isDeviceOnline) {
                        isDeviceOnline = false;
                        Log.e(TAG, "⚠ 设备离线（连续" + consecutiveFailures + "次失败），停止自动轮询");
                        if (dataFetcher != null) {
                            dataFetcher.stopPolling();
                        }
                        notifyAllFragments();
                    }
                }
            }

            @Override
            public void onControlSuccess(String property, Object value) {
                runOnUiThread(() -> {
                    String message;
                    if (property.equals("temp_threshold")) message = "温度阈值: " + value + "°C";
                    else if (property.equals("smoke_threshold")) message = "烟雾阈值: " + value;
                    else if (property.equals("pm25_threshold")) message = "PM2.5阈值: " + value + "μg/m³";
                    else if (property.equals("fan_en") || property.equals("fan")) message = "风扇" + (value.equals(true) ? " 开启" : " 关闭") + " 成功";
                    else if (property.equals("beep")) message = "蜂鸣器" + (value.equals(true) ? " 开启" : " 关闭") + " 成功";
                    else if (property.equals("led")) message = "LED" + (value.equals(true) ? " 开启" : " 关闭") + " 成功";
                    else if (property.equals("steeringstatus")) message = "舵机角度调节成功";
                    else message = property + "=" + value + " 设置成功";
                    Toast.makeText(MainActivity.this, message, Toast.LENGTH_SHORT).show();
                });
            }

            @Override
            public void onDataDelay(boolean hasDelay) {
                Log.d(TAG, hasDelay ? "⚠ 数据延迟" : "✓ 数据正常");
            }
        });

        dataFetcher.startPolling();

        JSONObject cachedData = dataFetcher.getCachedData();
        if (cachedData != null) {
            Log.d(TAG, "[缓存] 加载缓存数据: " + cachedData.toString());
            try {
                JSONArray parsedData = DataFetcher.parseResponse(cachedData);
                parseDeviceData(parsedData, "缓存");
            } catch (JSONException e) {
                Log.e(TAG, "[缓存] 解析缓存数据失败", e);
            }
        }

        Log.d(TAG, "DataFetcher初始化完成");
    }

    private void parseDeviceData(JSONArray data, String dataSource) throws JSONException {
        boolean hasNewData = false;
        int envDataCount = 0, statusDataCount = 0;

        for (int i = 0; i < data.length(); i++) {
            JSONObject value = data.getJSONObject(i);
            String identifier = value.optString("identifier");
            String val = value.optString("value");
            if (val == null || val.isEmpty()) {
                if (value.has("value")) val = String.valueOf(value.get("value"));
            }

            Log.d(TAG, "[" + dataSource + "] 解析数据 - identifier: " + identifier + ", value: " + val);

            if (identifier.equals("temperature")) {
                temperature_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("humidity")) {
                humidity_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("light")) {
                light_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("pm2_5") || identifier.equals("pm2.5") || identifier.equals("PM2.5")) {
                pm2_5_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("mq2") || identifier.equals("smoke")) {
                mq2_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("flame")) {
                flame_value = val; hasNewData = true; envDataCount++;
            } else if (identifier.equals("alarm_state")) {
                try { alarm_state = Integer.parseInt(val); hasNewData = true; statusDataCount++;
                } catch (NumberFormatException e) { Log.e(TAG, "[" + dataSource + "] 解析告警状态失败: " + val); }
            } else if (identifier.equals("alarm_type")) {
                alarm_type = val; hasNewData = true; statusDataCount++;
            } else if (identifier.equals("vibration")) {
                vibration_value = val; hasNewData = true; statusDataCount++;
            } else if (identifier.equals("tilt_angle")) {
                tilt_angle_value = val; hasNewData = true; statusDataCount++;
            } else if (identifier.equals("led")) {
                led_state = val.equals("1") || val.equalsIgnoreCase("true"); hasNewData = true; statusDataCount++;
            } else if (identifier.equals("beep")) {
                beep_state = val.equals("1") || val.equalsIgnoreCase("true"); hasNewData = true; statusDataCount++;
            } else if (identifier.equals("fan_en") || identifier.equals("fan")) {
                boolean fanOn = val.equals("1") || val.equalsIgnoreCase("true");
                fan_state = fanOn;
                fan_status = fanOn ? "运行中" : "已停止"; hasNewData = true; statusDataCount++;
            } else if (identifier.equals("fan_status")) {
                fan_status = (val.equals("1") || val.equalsIgnoreCase("true")) ? "运行中" : "已停止"; hasNewData = true; statusDataCount++;
            } else if (identifier.equals("work_mode")) {
                work_mode_auto = val.equals("1") || val.equalsIgnoreCase("true") || val.equals("auto");
                hasNewData = true; statusDataCount++;
            } else if (identifier.equals("relay")) {
                if (val != null && !val.isEmpty()) {
                    boolean relayOn = val.equals("1") || val.equalsIgnoreCase("true");
                    fan_state = relayOn;
                    fan_status = relayOn ? "运行中" : "已停止";
                }
                hasNewData = true; statusDataCount++;
            } else if (identifier.equals("temp_threshold")) {
                try {
                    temp_threshold = (int) Float.parseFloat(val);
                    if (temp_threshold < 0) temp_threshold = 0;
                    hasNewData = true; statusDataCount++;
                    isThresholdCommandPending = false;
                    if (thresholdTimeoutRunnable != null) {
                        timeoutHandler.removeCallbacks(thresholdTimeoutRunnable);
                        thresholdTimeoutRunnable = null;
                    }
                } catch (NumberFormatException e) { Log.e(TAG, "[" + dataSource + "] 解析温度阈值失败: " + val); }
            } else if (identifier.equals("smoke_threshold") || identifier.equals("mq2_threshold")) {
                try { smoke_threshold = (int) Float.parseFloat(val); hasNewData = true; statusDataCount++;
                    isSmokeThresholdPending = false;
                } catch (NumberFormatException e) { Log.e(TAG, "[" + dataSource + "] 解析烟雾阈值失败: " + val); }
            } else if (identifier.equals("pm25_threshold")) {
                try { pm25_threshold = (int) Float.parseFloat(val); hasNewData = true; statusDataCount++;
                    isPm25ThresholdPending = false;
                } catch (NumberFormatException e) { Log.e(TAG, "[" + dataSource + "] 解析PM2.5阈值失败: " + val); }
            } else if (identifier.equals("steeringstatus")) {
                try {
                    steering_angle = (int) Float.parseFloat(val);
                    if (steering_angle < 0) steering_angle = 0;
                    if (steering_angle > 180) steering_angle = 180;
                    hasNewData = true; statusDataCount++;
                    isSteeringCommandPending = false;
                } catch (NumberFormatException e) { Log.e(TAG, "[" + dataSource + "] 解析舵机角度失败: " + val); }
            }
        }

        if (hasNewData) {
            lastDataReceiveTime = System.currentTimeMillis();
            isDeviceOnline = true;
            Log.d(TAG, "[" + dataSource + "] 数据接收完成 - 环境: " + envDataCount + " 条, 状态: " + statusDataCount + " 条");
            if (envDataCount == 0) {
                Log.w(TAG, "[" + dataSource + "] 警告: 未收到环境数据");
            }
            notifyAllFragments();

            checkAndSendAlarmNotification();

            writeSensorRecordToRoom();
        }
    }

    private void writeSensorRecordToRoom() {
        long now = System.currentTimeMillis();
        boolean timeToWrite = (now - lastRoomWriteTime) >= ROOM_WRITE_INTERVAL_MS;
        boolean alarmChanged = (alarm_state != lastWrittenAlarmState);

        if (timeToWrite || alarmChanged) {
            lastRoomWriteTime = now;
            lastWrittenAlarmState = alarm_state;

            roomWriteExecutor.execute(() -> {
                try {
                    SensorRecord record = new SensorRecord();
                    record.timestamp = lastDataReceiveTime > 0 ? lastDataReceiveTime : now;

                    record.temperature = parseDouble(temperature_value);
                    record.humidity = parseDouble(humidity_value);
                    record.mq2 = parseDouble(mq2_value);
                    record.pm2_5 = parseDouble(pm2_5_value);
                    record.flame = parseDouble(flame_value);
                    record.alarmType = alarm_state;

                    AppDatabase.getInstance(MainActivity.this).sensorRecordDao().insert(record);
                    Log.d(TAG, "[Room] 写入历史记录: temp=" + record.temperature
                            + " smoke=" + record.mq2 + " alarm=" + record.alarmType);
                } catch (Exception e) {
                    Log.e(TAG, "[Room] 写入失败", e);
                }
            });
        }
    }

    private double parseDouble(String value) {
        if (value == null || value.isEmpty() || "--".equals(value)) return 0;
        try { return Double.parseDouble(value); }
        catch (NumberFormatException e) { return 0; }
    }

    public void controlDevice(String property, Object value) {
        if (property.equals("led")) led_state = (Boolean) value;
        else if (property.equals("beep")) beep_state = (Boolean) value;
        else if (property.equals("fan_en") || property.equals("fan")) fan_state = (Boolean) value;
        else if (property.equals("work_mode")) work_mode_auto = (Boolean) value;
        else if (property.equals("temp_threshold")) temp_threshold = (Integer) value;
        else if (property.equals("smoke_threshold")) smoke_threshold = (Integer) value;
        else if (property.equals("pm25_threshold")) pm25_threshold = (Integer) value;
        else if (property.equals("steeringstatus")) steering_angle = (Integer) value;

        notifyAllFragments();

        if (property.equals("steeringstatus") && gatewayIp != null && !gatewayIp.isEmpty()) {
            dataFetcher.sendLocalControlCommand(gatewayIp, property, (Integer) value);
            Log.d(TAG, "本地直连模式发送舵机指令: " + property + "=" + value);
        } else if (dataFetcher != null) {
            dataFetcher.sendControlCommand(property, value);
            Log.d(TAG, "通过HTTP发送控制指令: " + property + "=" + value);
        } else {
            Toast.makeText(this, "数据获取器未初始化", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (dataFetcher != null) dataFetcher.stopPolling();
        if (timeoutHandler != null) timeoutHandler.removeCallbacksAndMessages(null);
        if (roomWriteExecutor != null) roomWriteExecutor.shutdownNow();
    }

    private void checkAndRequestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                new AlertDialog.Builder(this)
                        .setTitle("开启通知权限")
                        .setMessage("为了及时接收充电棚的消防告警通知，需要您开启通知权限。\n\n点击「去设置」→ 通知 → 开启")
                        .setPositiveButton("去设置", (dialog, which) -> {
                            Intent intent = new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS);
                            intent.putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName());
                            startActivity(intent);
                        })
                        .setNegativeButton("稍后再说", null)
                        .setCancelable(false)
                        .show();
            }
        }
    }

    public void setGatewayIp(String ip) {
        this.gatewayIp = ip;
        if (ip != null && !ip.isEmpty()) {
            Log.d(TAG, "已切换到本地直连模式，网关IP: " + ip);
        } else {
            Log.d(TAG, "已切换到云端监控模式");
        }
    }

    public String getGatewayIp() {
        return gatewayIp;
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID,
                    "消防告警",
                    NotificationManager.IMPORTANCE_HIGH
            );
            channel.setDescription("充电棚消防网关告警通知");
            channel.enableVibration(true);
            channel.enableLights(true);
            NotificationManager manager = getSystemService(NotificationManager.class);
            if (manager != null) manager.createNotificationChannel(channel);
        }
    }

    private void checkAndSendAlarmNotification() {
        if (alarm_state == 0) {
            lastNotifiedAlarmState = 0;
            return;
        }
        if (alarm_state == lastNotifiedAlarmState) return;
        lastNotifiedAlarmState = alarm_state;

        String title, body;
        switch (alarm_state) {
            case 1:
                title = "⚠️ 温度报警";
                body = "充电棚温度超过阈值 (" + temp_threshold + "°C)，请及时检查！";
                break;
            case 2:
                title = "🔥 烟雾/火焰报警";
                body = "充电棚检测到烟雾或火焰，请立即处理！";
                break;
            case 3:
                title = "🌫️ PM2.5超标";
                body = "充电棚PM2.5超过阈值 (" + pm25_threshold + "μg/m³)";
                break;
            case 4:
                title = "📳 振动告警";
                body = "充电棚检测到异常振动，请检查设备安全！";
                break;
            case 5:
                title = "🚨 火警触发！";
                body = "检测到火情，请立即疏散并检查充电棚！";
                break;
            default:
                title = "⚠️ 设备告警";
                body = "充电棚发生未知告警，请检查设备状态";
                break;
        }

        Intent intent = new Intent(this, MainActivity.class);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        PendingIntent pendingIntent = PendingIntent.getActivity(this, 0, intent,
                PendingIntent.FLAG_UPDATE_CURRENT | (Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0));

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_circle_red)
                .setContentTitle(title)
                .setContentText(body)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setContentIntent(pendingIntent)
                .setCategory(NotificationCompat.CATEGORY_ALARM);

        NotificationManagerCompat.from(this).notify(ALARM_NOTIFICATION_ID, builder.build());
    }

    private static class FragmentAdapter extends androidx.viewpager2.adapter.FragmentStateAdapter {
        private final List<Fragment> fragments;

        FragmentAdapter(@NonNull AppCompatActivity activity, List<Fragment> fragments) {
            super(activity);
            this.fragments = fragments;
        }

        @NonNull
        @Override
        public Fragment createFragment(int position) {
            return fragments.get(position);
        }

        @Override
        public int getItemCount() {
            return fragments.size();
        }
    }
}

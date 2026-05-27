package com.example.onenet215;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.cardview.widget.CardView;
import androidx.fragment.app.Fragment;

public class FragmentStatus extends Fragment {

    private TextView tvConnectionStatus, tvLastUpdate;
    private TextView tvAlarmState, tvAlarmDetail;
    private TextView tvFanStatus, tvSteeringAngle;
    private CardView cardAlarm;
    private ImageView ivAlarmIndicator;
    private Button btnRefresh;

    private String fan_status, steering_angle_value;
    private int alarm_state;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_status, container, false);

        tvConnectionStatus = view.findViewById(R.id.tvConnectionStatus);
        tvLastUpdate = view.findViewById(R.id.tvLastUpdate);
        tvAlarmState = view.findViewById(R.id.tvAlarmState);
        tvAlarmDetail = view.findViewById(R.id.tvAlarmDetail);
        tvFanStatus = view.findViewById(R.id.tvFanStatus);
        tvSteeringAngle = view.findViewById(R.id.tvSteeringAngle);
        cardAlarm = view.findViewById(R.id.cardAlarm);
        ivAlarmIndicator = view.findViewById(R.id.ivAlarmIndicator);
        btnRefresh = view.findViewById(R.id.btnRefresh);

        btnRefresh.setOnClickListener(v -> {
            Toast.makeText(getContext(), "正在获取最新数据", Toast.LENGTH_SHORT).show();
            getMain().requestRefresh();
        });

        return view;
    }

    private MainActivity getMain() {
        return (MainActivity) getActivity();
    }

    public void updateStatusData(String fan, String steeringAngle) {
        this.fan_status = fan;
        this.steering_angle_value = steeringAngle;
    }

    public void updateAlarmState(int state, int tempThreshold, int pm25Threshold) {
        this.alarm_state = state;

        if (tvAlarmState == null) return;

        int cardColor, textColor;
        String alarmText, alarmDetail;

        switch (state) {
            case 0:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_green);
                alarmText = "系统正常";
                alarmDetail = "充电棚运行中 · 无异常";
                cardColor = 0xFFC8E6C9;
                textColor = 0xFF2E7D32;
                break;
            case 1:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 温度报警";
                alarmDetail = "温度超过阈值 (" + tempThreshold + "°C)";
                cardColor = 0xFFFFCDD2;
                textColor = 0xFFC62828;
                break;
            case 2:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 烟雾/火焰报警";
                alarmDetail = "检测到烟雾或火焰";
                cardColor = 0xFFFFCDD2;
                textColor = 0xFFC62828;
                break;
            case 3:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ PM2.5超标";
                alarmDetail = "PM2.5超过阈值 (" + pm25Threshold + "μg/m³)";
                cardColor = 0xFFFFE0B2;
                textColor = 0xFFE65100;
                break;
            case 4:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "⚠ 振动告警";
                alarmDetail = "检测到异常振动";
                cardColor = 0xFFFFE0B2;
                textColor = 0xFFE65100;
                break;
            case 5:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_red);
                alarmText = "【检测到火情】";
                alarmDetail = "火警触发！请立即疏散并检查充电棚";
                cardColor = 0xFFEF5350;
                textColor = 0xFFFFFFFF;
                break;
            default:
                ivAlarmIndicator.setImageResource(R.drawable.ic_circle_green);
                alarmText = "系统正常";
                alarmDetail = "充电棚运行中 · 无异常";
                cardColor = 0xFFC8E6C9;
                textColor = 0xFF2E7D32;
                break;
        }

        tvAlarmState.setText(alarmText);
        tvAlarmDetail.setText(alarmDetail);
        tvAlarmState.setTextColor(textColor);
        tvAlarmDetail.setTextColor(textColor);
        cardAlarm.setCardBackgroundColor(cardColor);
    }

    public void updateLastTime(String time) {
        if (tvLastUpdate != null) tvLastUpdate.setText(time);
    }

    public void updateConnectionStatus(boolean online) {
        if (tvConnectionStatus != null) {
            tvConnectionStatus.setText(online ? "已连接" : "未连接");
            tvConnectionStatus.setTextColor(online ? 0xFF4CAF50 : 0xFFF44336);
        }
    }

    public void refreshUI(boolean isOnline) {
        if (tvFanStatus == null) return;

        tvFanStatus.setText(fan_status != null && !fan_status.isEmpty() ? fan_status : "--");
        tvSteeringAngle.setText(steering_angle_value != null ? steering_angle_value : "--");

        if (isOnline) {
            tvFanStatus.setTextColor(0xFF00897B);
            tvSteeringAngle.setTextColor(0xFF1565C0);
        } else {
            int gray = 0xFFBDBDBD;
            tvFanStatus.setTextColor(gray);
            tvSteeringAngle.setTextColor(gray);
        }
    }
}

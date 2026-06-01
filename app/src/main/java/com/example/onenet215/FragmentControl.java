package com.example.onenet215;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ToggleButton;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;

public class FragmentControl extends Fragment {

    private Button btnLedOn, btnLedOff;
    private Switch swBeep, swFan;
    private ToggleButton tbtnWorkMode;
    private SeekBar sbTempThreshold, sbSmokeThreshold, sbPm25Threshold;
    private TextView tvTempThresholdValue, tvSmokeThresholdValue, tvPm25ThresholdValue;
    private TextView tvCurrentTempThreshold, tvCurrentSmokeThreshold, tvCurrentPm25Threshold;
    private EditText etTempThreshold, etSmokeThreshold, etPm25Threshold;
    private Button btnTempThreshold, btnSmokeThreshold, btnPm25Threshold;
    private SeekBar sbSteeringAngle;
    private TextView tvSteeringAngleValue;
    private Button btnSteeringAngle;
    private com.google.android.material.button.MaterialButton btnSteeringPreset0, btnSteeringPreset45,
            btnSteeringPreset90, btnSteeringPreset135, btnSteeringPreset180;

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                             @Nullable Bundle savedInstanceState) {
        View view = inflater.inflate(R.layout.fragment_control, container, false);

        btnLedOn = view.findViewById(R.id.btnLedOn);
        btnLedOff = view.findViewById(R.id.btnLedOff);
        swBeep = view.findViewById(R.id.swBeep);
        swFan = view.findViewById(R.id.swFan);
        tbtnWorkMode = view.findViewById(R.id.tbtnWorkMode);
        sbTempThreshold = view.findViewById(R.id.sbTempThreshold);
        tvTempThresholdValue = view.findViewById(R.id.tvTempThresholdValue);
        sbSmokeThreshold = view.findViewById(R.id.sbSmokeThreshold);
        tvSmokeThresholdValue = view.findViewById(R.id.tvSmokeThresholdValue);
        sbPm25Threshold = view.findViewById(R.id.sbPm25Threshold);
        tvPm25ThresholdValue = view.findViewById(R.id.tvPm25ThresholdValue);
        etTempThreshold = view.findViewById(R.id.etTempThreshold);
        btnTempThreshold = view.findViewById(R.id.btnTempThreshold);
        etSmokeThreshold = view.findViewById(R.id.etSmokeThreshold);
        btnSmokeThreshold = view.findViewById(R.id.btnSmokeThreshold);
        etPm25Threshold = view.findViewById(R.id.etPm25Threshold);
        btnPm25Threshold = view.findViewById(R.id.btnPm25Threshold);
        tvCurrentTempThreshold = view.findViewById(R.id.tvCurrentTempThreshold);
        tvCurrentSmokeThreshold = view.findViewById(R.id.tvCurrentSmokeThreshold);
        tvCurrentPm25Threshold = view.findViewById(R.id.tvCurrentPm25Threshold);
        sbSteeringAngle = view.findViewById(R.id.sbSteeringAngle);
        tvSteeringAngleValue = view.findViewById(R.id.tvSteeringAngleValue);
        btnSteeringAngle = view.findViewById(R.id.btnSteeringAngle);

        btnSteeringPreset0 = view.findViewById(R.id.btnSteeringPreset0);
        btnSteeringPreset45 = view.findViewById(R.id.btnSteeringPreset45);
        btnSteeringPreset90 = view.findViewById(R.id.btnSteeringPreset90);
        btnSteeringPreset135 = view.findViewById(R.id.btnSteeringPreset135);
        btnSteeringPreset180 = view.findViewById(R.id.btnSteeringPreset180);

        view.findViewById(R.id.btnLogout).setOnClickListener(v -> {
            getActivity().getSharedPreferences("auth_prefs", 0)
                    .edit().putBoolean("is_logged_in", false).apply();
            startActivity(new Intent(getActivity(), LoginActivity.class));
            getActivity().finish();
        });

        // LED
        btnLedOn.setOnClickListener(v -> getMain().controlDevice("led", true));
        btnLedOff.setOnClickListener(v -> getMain().controlDevice("led", false));

        // BEEP
        swBeep.setOnCheckedChangeListener((bv, isChecked) -> {
            getMain().beep_state = isChecked;
            swBeep.setEnabled(false);
            new Handler(Looper.getMainLooper()).postDelayed(() -> swBeep.setEnabled(true), 1000);
            getMain().controlDevice("beep", isChecked);
        });

        // 风扇
        swFan.setOnCheckedChangeListener((bv, isChecked) -> {
            getMain().fan_state = isChecked;
            swFan.setEnabled(false);
            new Handler(Looper.getMainLooper()).postDelayed(() -> swFan.setEnabled(true), 1000);
            getMain().controlDevice("fan_en", isChecked);
        });

        // 工作模式
        tbtnWorkMode.setOnCheckedChangeListener((bv, isChecked) -> {
            boolean auto = !isChecked;
            getMain().work_mode_auto = auto;
            getMain().controlDevice("work_mode", auto);
            Toast.makeText(getContext(), "切换到" + (auto ? "自动模式" : "手动模式"), Toast.LENGTH_SHORT).show();
        });

        // 温度阈值 SeekBar
        sbTempThreshold.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvTempThresholdValue.setText(progress + "°C");
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                getMain().isThresholdCommandPending = true;
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                int val = seekBar.getProgress();
                getMain().temp_threshold = val;
                getMain().controlDevice("temp_threshold", val);
                Toast.makeText(getContext(), "温度阈值: " + val + "°C", Toast.LENGTH_SHORT).show();
            }
        });

        // 烟雾阈值 SeekBar
        sbSmokeThreshold.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvSmokeThresholdValue.setText(String.valueOf(progress));
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                getMain().isSmokeThresholdPending = true;
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                getMain().smoke_threshold = seekBar.getProgress();
                getMain().controlDevice("mq2_threshold", getMain().smoke_threshold);
                Toast.makeText(getContext(), "烟雾阈值: " + getMain().smoke_threshold, Toast.LENGTH_SHORT).show();
            }
        });

        // PM2.5阈值 SeekBar
        sbPm25Threshold.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvPm25ThresholdValue.setText(progress + " μg/m³");
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                getMain().isPm25ThresholdPending = true;
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                getMain().pm25_threshold = seekBar.getProgress();
                getMain().controlDevice("pm25_threshold", getMain().pm25_threshold);
                Toast.makeText(getContext(), "PM2.5阈值: " + getMain().pm25_threshold + " μg/m³", Toast.LENGTH_SHORT).show();
            }
        });

        // 手动输入按钮
        btnTempThreshold.setOnClickListener(v -> applyTempThresholdInput());
        etTempThreshold.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) { applyTempThresholdInput(); return true; }
            return false;
        });
        btnSmokeThreshold.setOnClickListener(v -> applySmokeThresholdInput());
        etSmokeThreshold.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) { applySmokeThresholdInput(); return true; }
            return false;
        });
        btnPm25Threshold.setOnClickListener(v -> applyPm25ThresholdInput());
        etPm25Threshold.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) { applyPm25ThresholdInput(); return true; }
            return false;
        });

        sbSteeringAngle.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                tvSteeringAngleValue.setText(progress + "°");
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {
                getMain().isSteeringCommandPending = true;
            }
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) { }
        });

        btnSteeringAngle.setOnClickListener(v -> {
            int angle = sbSteeringAngle.getProgress();
            getMain().steering_angle = angle;
            getMain().controlDevice("steeringstatus", angle);
        });

        btnSteeringPreset0.setOnClickListener(v -> applySteeringPreset(0));
        btnSteeringPreset45.setOnClickListener(v -> applySteeringPreset(45));
        btnSteeringPreset90.setOnClickListener(v -> applySteeringPreset(90));
        btnSteeringPreset135.setOnClickListener(v -> applySteeringPreset(135));
        btnSteeringPreset180.setOnClickListener(v -> applySteeringPreset(180));

        return view;
    }

    private MainActivity getMain() {
        return (MainActivity) getActivity();
    }

    private void applyTempThresholdInput() {
        String input = etTempThreshold.getText().toString().trim();
        if (TextUtils.isEmpty(input)) { Toast.makeText(getContext(), "请输入温度阈值", Toast.LENGTH_SHORT).show(); return; }
        try {
            int value = Integer.parseInt(input);
            if (value < 0 || value > 100) { Toast.makeText(getContext(), "范围 0-100°C", Toast.LENGTH_SHORT).show(); return; }
            getMain().temp_threshold = value;
            getMain().isThresholdCommandPending = true;
            getMain().controlDevice("temp_threshold", value);
            sbTempThreshold.setProgress(value);
            tvTempThresholdValue.setText(value + "°C");
            etTempThreshold.setText("");
            closeKeyboard();
            Toast.makeText(getContext(), "温度阈值: " + value + "°C", Toast.LENGTH_SHORT).show();
        } catch (NumberFormatException e) { Toast.makeText(getContext(), "请输入有效数字", Toast.LENGTH_SHORT).show(); }
    }

    private void applySmokeThresholdInput() {
        String input = etSmokeThreshold.getText().toString().trim();
        if (TextUtils.isEmpty(input)) { Toast.makeText(getContext(), "请输入烟雾阈值", Toast.LENGTH_SHORT).show(); return; }
        try {
            int value = Integer.parseInt(input);
            if (value < 0 || value > 4000) { Toast.makeText(getContext(), "范围 0-4000", Toast.LENGTH_SHORT).show(); return; }
            getMain().smoke_threshold = value;
            getMain().isSmokeThresholdPending = true;
            getMain().controlDevice("mq2_threshold", value);
            sbSmokeThreshold.setProgress(value);
            tvSmokeThresholdValue.setText(String.valueOf(value));
            etSmokeThreshold.setText("");
            closeKeyboard();
            Toast.makeText(getContext(), "烟雾阈值: " + value, Toast.LENGTH_SHORT).show();
        } catch (NumberFormatException e) { Toast.makeText(getContext(), "请输入有效数字", Toast.LENGTH_SHORT).show(); }
    }

    private void applyPm25ThresholdInput() {
        String input = etPm25Threshold.getText().toString().trim();
        if (TextUtils.isEmpty(input)) { Toast.makeText(getContext(), "请输入PM2.5阈值", Toast.LENGTH_SHORT).show(); return; }
        try {
            int value = Integer.parseInt(input);
            if (value < 0 || value > 500) { Toast.makeText(getContext(), "范围 0-500 μg/m³", Toast.LENGTH_SHORT).show(); return; }
            getMain().pm25_threshold = value;
            getMain().isPm25ThresholdPending = true;
            getMain().controlDevice("pm25_threshold", value);
            sbPm25Threshold.setProgress(value);
            tvPm25ThresholdValue.setText(value + " μg/m³");
            etPm25Threshold.setText("");
            closeKeyboard();
            Toast.makeText(getContext(), "PM2.5阈值: " + value + " μg/m³", Toast.LENGTH_SHORT).show();
        } catch (NumberFormatException e) { Toast.makeText(getContext(), "请输入有效数字", Toast.LENGTH_SHORT).show(); }
    }

    private void closeKeyboard() {
        android.view.inputmethod.InputMethodManager imm = (android.view.inputmethod.InputMethodManager)
                requireContext().getSystemService(android.content.Context.INPUT_METHOD_SERVICE);
        View focus = getActivity() != null ? getActivity().getCurrentFocus() : null;
        if (imm != null && focus != null) imm.hideSoftInputFromWindow(focus.getWindowToken(), 0);
    }

    private void applySteeringPreset(int angle) {
        sbSteeringAngle.setProgress(angle);
        tvSteeringAngleValue.setText(angle + "°");
        getMain().steering_angle = angle;
        getMain().controlDevice("steeringstatus", angle);
    }

    public void refreshUI(boolean isOnline) {
        if (btnLedOn == null || getActivity() == null) return;

        boolean led = getMain().led_state;
        btnLedOn.setBackgroundTintList(android.content.res.ColorStateList.valueOf(led ? 0xFF4CAF50 : 0xFF9E9E9E));
        btnLedOff.setBackgroundTintList(android.content.res.ColorStateList.valueOf(led ? 0xFF9E9E9E : 0xFF4CAF50));

        tvCurrentTempThreshold.setText(getMain().temp_threshold + "°C");
        tvCurrentSmokeThreshold.setText(String.valueOf(getMain().smoke_threshold));
        tvCurrentPm25Threshold.setText(getMain().pm25_threshold + " μg/m³");

        if (swBeep.isEnabled() && swBeep.isChecked() != getMain().beep_state) {
            swBeep.setChecked(getMain().beep_state);
        }
        if (swFan.isEnabled() && swFan.isChecked() != getMain().fan_state) {
            swFan.setChecked(getMain().fan_state);
        }

        if (!getMain().isThresholdCommandPending) {
            sbTempThreshold.setProgress(getMain().temp_threshold);
            tvTempThresholdValue.setText(getMain().temp_threshold + "°C");
        }
        if (!getMain().isSmokeThresholdPending) {
            sbSmokeThreshold.setProgress(getMain().smoke_threshold);
            tvSmokeThresholdValue.setText(String.valueOf(getMain().smoke_threshold));
        }
        if (!getMain().isPm25ThresholdPending) {
            sbPm25Threshold.setProgress(getMain().pm25_threshold);
            tvPm25ThresholdValue.setText(getMain().pm25_threshold + " μg/m³");
        }

        if (!getMain().isSteeringCommandPending) {
            sbSteeringAngle.setProgress(getMain().steering_angle);
            tvSteeringAngleValue.setText(getMain().steering_angle + "°");
        }

        tbtnWorkMode.setChecked(!getMain().work_mode_auto);

        setControlsEnabled(isOnline);
    }

    private void setControlsEnabled(boolean enabled) {
        float alpha = enabled ? 1.0f : 0.5f;
        swBeep.setEnabled(enabled); swBeep.setAlpha(alpha);
        swFan.setEnabled(enabled); swFan.setAlpha(alpha);
        tbtnWorkMode.setEnabled(enabled); tbtnWorkMode.setAlpha(alpha);
        sbTempThreshold.setEnabled(enabled); sbTempThreshold.setAlpha(alpha);
        sbSmokeThreshold.setEnabled(enabled); sbSmokeThreshold.setAlpha(alpha);
        sbPm25Threshold.setEnabled(enabled); sbPm25Threshold.setAlpha(alpha);
        btnLedOn.setEnabled(enabled); btnLedOn.setAlpha(alpha);
        btnLedOff.setEnabled(enabled); btnLedOff.setAlpha(alpha);
        sbSteeringAngle.setEnabled(enabled); sbSteeringAngle.setAlpha(alpha);
        btnSteeringAngle.setEnabled(enabled); btnSteeringAngle.setAlpha(alpha);
    }
}

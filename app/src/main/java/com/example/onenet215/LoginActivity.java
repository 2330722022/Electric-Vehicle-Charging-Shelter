package com.example.onenet215;

import android.content.Intent;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.View;
import android.widget.CheckBox;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.button.MaterialButton;
import com.google.android.material.textfield.TextInputEditText;

import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

public class LoginActivity extends AppCompatActivity {

    private TextInputEditText etUsername, etPassword;
    private CheckBox cbRemember;
    private MaterialButton btnLogin;
    private TextView tvLoginStatus, tvGoRegister;
    private final Executor ioExecutor = Executors.newSingleThreadExecutor();
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        prefs = getSharedPreferences("auth_prefs", MODE_PRIVATE);

        if (prefs.getBoolean("is_logged_in", false)) {
            startActivity(new Intent(this, MainActivity.class));
            finish();
            return;
        }

        setContentView(R.layout.activity_login);

        etUsername = findViewById(R.id.etUsername);
        etPassword = findViewById(R.id.etPassword);
        cbRemember = findViewById(R.id.cbRemember);
        btnLogin = findViewById(R.id.btnLogin);
        tvLoginStatus = findViewById(R.id.tvLoginStatus);
        tvGoRegister = findViewById(R.id.tvGoRegister);

        if (prefs.getBoolean("remembered", false)) {
            etUsername.setText(prefs.getString("saved_username", ""));
            etPassword.setText(prefs.getString("saved_password", ""));
            cbRemember.setChecked(true);
        }

        tvGoRegister.setOnClickListener(v -> {
            startActivity(new Intent(this, RegisterActivity.class));
        });

        btnLogin.setOnClickListener(v -> performLogin());
    }

    private void performLogin() {
        String username = etUsername.getText() != null ? etUsername.getText().toString().trim() : "";
        String password = etPassword.getText() != null ? etPassword.getText().toString().trim() : "";

        if (TextUtils.isEmpty(username)) {
            showStatus("请输入账号");
            return;
        }
        if (TextUtils.isEmpty(password)) {
            showStatus("请输入密码");
            return;
        }

        btnLogin.setEnabled(false);
        btnLogin.setText("登录中...");
        tvLoginStatus.setVisibility(View.GONE);

        ioExecutor.execute(() -> {
            AppDatabase db = AppDatabase.getInstance(this);
            User user = db.userDao().login(username, password);

            runOnUiThread(() -> {
                btnLogin.setEnabled(true);
                btnLogin.setText("登  录");

                if (user != null) {
                    SharedPreferences.Editor editor = prefs.edit();
                    editor.putBoolean("is_logged_in", true);
                    editor.putString("current_username", user.username);
                    editor.putString("current_role", user.role);

                    if (cbRemember.isChecked()) {
                        editor.putBoolean("remembered", true);
                        editor.putString("saved_username", username);
                        editor.putString("saved_password", password);
                    } else {
                        editor.putBoolean("remembered", false);
                        editor.remove("saved_username");
                        editor.remove("saved_password");
                    }
                    editor.apply();

                    Toast.makeText(this, "登录成功", Toast.LENGTH_SHORT).show();
                    startActivity(new Intent(this, MainActivity.class));
                    finish();
                } else {
                    showStatus("账号或密码错误");
                }
            });
        });
    }

    private void showStatus(String msg) {
        tvLoginStatus.setText(msg);
        tvLoginStatus.setVisibility(View.VISIBLE);
    }
}

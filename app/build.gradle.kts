plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.example.onenet215"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.example.onenet215"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
}

dependencies {
    implementation(libs.activity.ktx)
    implementation(libs.appcompat)
    implementation(libs.constraintlayout)
    implementation(libs.material)
    implementation(libs.cardview)
    implementation(libs.viewpager2)
    implementation(libs.mqtt.client)
    implementation(libs.mqtt.android.service)
    implementation(libs.okhttp)

    implementation(libs.room.runtime)
    annotationProcessor(libs.room.compiler)

    implementation(libs.mpandroidchart)

    testImplementation(libs.junit)
    androidTestImplementation(libs.espresso.core)
    androidTestImplementation(libs.ext.junit)
}
plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

android {
    namespace = "com.team.smartwatch"
    compileSdk {
        version = release(37)
    }

    defaultConfig {
        applicationId = "com.team.smartwatch.prototype"
        minSdk = 24
        targetSdk = 37
        versionCode = 9
        versionName = "1.7.1-time-sync-sim"
        buildConfigField("boolean", "NEARBY_PERMISSION_PREVIEW", "false")

    }

    buildTypes {
        create("permissionPreview") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".permissions"
            versionNameSuffix = "-permission-preview"
            matchingFallbacks += listOf("debug")
            buildConfigField("boolean", "NEARBY_PERMISSION_PREVIEW", "true")
        }
        release {
            optimization {
                enable = true
                packageScope = setOf("androidx.**", "kotlin.**", "kotlinx.**")
            }
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_11
        targetCompatibility = JavaVersion.VERSION_11
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
}

dependencies {
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    debugImplementation(libs.androidx.compose.ui.tooling)
}

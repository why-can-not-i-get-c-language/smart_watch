import com.android.build.api.variant.HostTestBuilder
import java.security.KeyStore
import java.security.MessageDigest
import java.util.HexFormat

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

val developmentKeystore = providers.environmentVariable("SMARTWATCH_DEVELOPMENT_KEYSTORE")
    .orElse(providers.systemProperty("user.home").map { "$it/.android/smartwatch-development.keystore" })
    .map { File(it) }
    .get()
abstract class VerifyDevelopmentSigning : DefaultTask() {
    @get:Input
    abstract val keystorePath: Property<String>

    @TaskAction
    fun verify() {
        val keystore = File(keystorePath.get())
        check(keystore.isFile) { "缺少智能手表开发签名密钥。请查阅 Android 开发包签名说明。" }
        val store = KeyStore.getInstance(keystore, KEY_PASSWORD.toCharArray())
        val certificate = store.getCertificate(KEY_ALIAS)
        val fingerprint = certificate?.let {
            HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(it.encoded))
        }
        check(store.isKeyEntry(KEY_ALIAS) && fingerprint == EXPECTED_CERTIFICATE) {
            "智能手表开发签名与旧版不一致，无法生成覆盖更新包。"
        }
    }

    companion object {
        // 旧调试版本使用 Android 默认调试密钥凭据，私钥文件在工程外。
        const val KEY_ALIAS = "androiddebugkey"
        const val KEY_PASSWORD = "android"
        private const val EXPECTED_CERTIFICATE = "6b8dfa7d16f298afb726c1939a589a29e72a1122b6e38c298981c7299e2e9072"
    }
}

val verifyDevelopmentSigning by tasks.registering(VerifyDevelopmentSigning::class) {
    keystorePath.set(developmentKeystore.absolutePath)
}

tasks.matching {
    it.name in setOf("validateSigningDebug", "validateSigningPermissionPreview", "validateSigningDeviceIntegration")
}.configureEach {
    dependsOn(verifyDevelopmentSigning)
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
        versionCode = 21
        versionName = "1.8.9"
        buildConfigField("boolean", "NEARBY_PERMISSION_PREVIEW", "false")
        buildConfigField("boolean", "D3_FAULT_INJECTION", "false")
    }

    signingConfigs {
        create("smartwatchDevelopment") {
            storeFile = developmentKeystore
            storePassword = VerifyDevelopmentSigning.KEY_PASSWORD
            keyAlias = VerifyDevelopmentSigning.KEY_ALIAS
            keyPassword = VerifyDevelopmentSigning.KEY_PASSWORD
        }
    }
    buildTypes {
        getByName("debug") {
            signingConfig = signingConfigs.getByName("smartwatchDevelopment")
        }
        create("permissionPreview") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".permissions"
            versionNameSuffix = "-permission-preview"
            signingConfig = signingConfigs.getByName("smartwatchDevelopment")
            matchingFallbacks += listOf("debug")
            buildConfigField("boolean", "NEARBY_PERMISSION_PREVIEW", "true")
        }
        create("deviceIntegration") {
            initWith(getByName("debug"))
            applicationIdSuffix = ".integration"
            versionNameSuffix = "-ble-integration"
            buildConfigField("boolean", "D3_FAULT_INJECTION", "true")
            signingConfig = signingConfigs.getByName("smartwatchDevelopment")
            matchingFallbacks += listOf("debug")
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

androidComponents {
    beforeVariants(selector().withBuildType("deviceIntegration")) {
        it.hostTests.getValue(HostTestBuilder.UNIT_TEST_TYPE).enable = true
    }
}

dependencies {
    testImplementation(libs.junit)
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

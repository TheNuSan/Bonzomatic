# How to Release version

*Note : Current release implementation is rustic, any imporvement is welcome*

## Setup

We need to add some secret environement variable on your project.

* Go to **Settings >> Secrets and Variables >> Actions >> Secrets**
* Click on **New repository secret** and add theses variables secret: 
 * NDI_SDK_URL="https://UrlToDownloadNDI"

## How to 

On the main page of the project :
* Click on **Releases** on the right
* Click on **Draft a new release**
* Click on **Choose a tag** and type a tag version with this format `v0.0.0` . Click on **+ Create new tag**
* On **Release Title** , put anything (it will be automatically replaced later if all good)
* You can write some message the Description field.
* Click on **Publish Release**

At that moment, github actions are triggered and should eventually build and add artifacts (zip files) into the release. You can check the process on **Actions > build-release**